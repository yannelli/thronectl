# thronectl wire protocol v1

The device (Cardputer ADV) is the client. The host daemon (`thronectl daemon`) is the server.

## Discovery

- UDP port `4787`.
- The device sends the ASCII datagram `THRONE?1` to `255.255.255.255:4787`.
- Each daemon replies to the sender with one JSON datagram:

```json
{"t":"throne","v":1,"id":"9f2c1a0b4d5e6f70","name":"Studio MacBook","port":4787,"os":"darwin"}
```

`id` is the host ID (see Identity). The device also accepts a manual `host:port` entry.

## Framing

- TCP port `4787` (same number as discovery).
- Each frame is a 4-byte big-endian length, then the payload. The maximum payload is 65536 bytes.
- Handshake frames are plaintext UTF-8 JSON.
- All frames after the handshake are encrypted (see Encryption).

## Bluetooth LE transport

The link can also run over Bluetooth LE. The device is the GATT peripheral. The daemon is the central. The bytes on the link are the same as on TCP: the frames in Framing, then the same handshake and encryption.

| UUID | Name | Properties | Content |
|------|------|------------|---------|
| `7b3e0001-6f1a-4c2e-9d6a-2f1c8e5a7d10` | service | | |
| `7b3e0002-6f1a-4c2e-9d6a-2f1c8e5a7d10` | RX | write without response | Bytes from the host to the device |
| `7b3e0003-6f1a-4c2e-9d6a-2f1c8e5a7d10` | TX | notify | Bytes from the device to the host |
| `7b3e0004-6f1a-4c2e-9d6a-2f1c8e5a7d10` | CREDIT | read, notify | u32 LE bytes the device has read from RX since the connection started, then u16 LE ATT MTU |

Hosts cache this layout, so it does not change between versions.

Advertising:

- The advert has the flags and the 128-bit service UUID. The scan response has the manufacturer data and the device name (up to 14 bytes).
- Manufacturer data, 13 bytes: company ID `0xFFFF` (u16 LE), ASCII `TC`, the 8-byte device ID (see Identity), and a flags byte. Bit 0 of the flags is set while the device's pairing window is open.
- The device advertises only while it has no link and no host is connected.

Connection:

- The daemon connects to a device that it has paired, or to a device that has the pairing flag set. It does not connect to a device that already has a session with it.
- After it connects, the daemon reads CREDIT, subscribes to CREDIT and then to TX. The device starts the handshake (`hello`) when the host subscribes to TX.
- A device refuses a host that it has not paired, unless its pairing window is open.
- Either side writes in packets of at most ATT MTU - 3 bytes. A packet can split a frame at any byte.

Flow control:

- The device reads RX into a 16384-byte buffer. The host keeps at most a window of bytes unread: 4096 on macOS, 12288 on other hosts. The unread count is bytes sent minus the CREDIT count.
- The device notifies CREDIT after each 1024 bytes it reads, when its buffer is empty, and when the MTU changes. A new connection starts the count at 0.
- If the host writes more than the buffer holds, the device closes the connection.

## Identity

- Each side has a static X25519 key pair. The device stores its key in NVS. The daemon stores its key in `~/.config/thronectl/identity.json`.
- ID = lowercase hex of the first 8 bytes of `SHA-256(static public key)`.
- Binary values in JSON are standard base64 with padding.

## Handshake

The handshake uses a commit-reveal step, so a man in the middle cannot pick keys that give the same pairing code on both sides.

1. Device to host, `hello`:

```json
{"t":"hello","v":1,"name":"Cardputer-1A2B","fw":"0.1.0","pk":"<32B>","epk":"<32B>","c":"<SHA-256(dn), 32B>"}
```

2. Host to device, `hello`:

```json
{"t":"hello","v":1,"id":"<host id>","name":"Studio MacBook","pk":"<32B>","epk":"<32B>","n":"<hn, 16B>"}
```

3. Device to host, `reveal`:

```json
{"t":"reveal","n":"<dn, 16B>","known":true}
```

`known` is true when the device has this host ID and the same host `pk` in its host list. When the device has the host ID with a different `pk`, it stops and shows "Host key changed".

4. The host checks `SHA-256(dn) == c`. If the check fails, the host closes the connection.

Both sides then compute:

```
th   = SHA-256("thronectl-v1" || dpk || depk || dn || hpk || hepk || hn)
ikm  = X25519(s_self, S_peer) || X25519(e_self, E_peer)       (64 bytes)
okm  = HKDF-SHA256(salt = th, ikm, info = "thronectl keys", L = 64)
k_dh = okm[0..32]    device-to-host key
k_hd = okm[32..64]   host-to-device key
sas  = u32be(SHA-256("thronectl-sas" || th)[0..4]) mod 1000000   (6 digits, zero padded)
```

`dpk`, `depk` are the device static and ephemeral public keys. `hpk`, `hepk` are the host keys.

## Encryption

- AES-256-GCM, 16-byte tag appended to the ciphertext.
- Nonce: 4 zero bytes, then the 8-byte big-endian frame counter. Each direction has its own counter. Each counter starts at 0.
- The AAD is empty.
- Plaintext: 1 kind byte, then the body.

| Kind | Body |
|------|------|
| `0x01` | UTF-8 JSON message |
| `0x02` | Microphone audio, device to host: PCM 16-bit LE mono |
| `0x03` | Speaker audio, host to device: PCM 16-bit LE mono |
| `0x04` | Firmware chunk, host to device: 4-byte big-endian offset, then the image bytes |

If a frame does not decrypt, the receiver closes the connection.

## Authorization

The first encrypted message from the host is `auth`.

- Trusted device, and `known` was true: `{"t":"auth","state":"ok","host":Host}`.
- Otherwise: `{"t":"auth","state":"pair","code":"482913"}`. The device shows the SAS it computed itself, and stops if `code` differs from it. The host asks the user to compare the code and answer yes or no (`thronectl pair`, or the daemon terminal).
  - Yes: the host stores the device public key. It sends `{"t":"auth","state":"ok","host":Host}`. The device stores `{id, name, pk, addr, port}` in NVS.
  - No, or no answer in 120 s: `{"t":"auth","state":"denied"}`, then the host closes the connection.

The device sends no requests before `state:"ok"`.

`Host`:

```json
{"id":"9f2c...","name":"Studio MacBook","hostname":"studio","os":"darwin","arch":"arm64","user":"dev","ver":"0.1.0","paseo":true,
 "tzOff":-240,"time":1761076800000}
```

`tzOff` is the host time zone in minutes east of UTC. `time` is the host clock in unix ms. The device uses them until NTP answers, and when its time zone setting is "From host".

## Messages

Requests (device to host):

```json
{"t":"req","id":7,"m":"agent.create","p":{...}}
```

Responses (host to device):

```json
{"t":"res","id":7,"ok":true,"r":{...}}
{"t":"res","id":7,"ok":false,"err":"no such agent"}
```

Events (host to device):

```json
{"t":"ev","e":"stats","d":{...}}
```

Keepalive: the device sends `ping` every 10 s. Either side closes the connection after 30 s with no frames.

Size limits: the device has no PSRAM, so the host sends no JSON message over 32 KB. An event that is too large is dropped. A reply that is too large becomes an error reply ("reply too large for the device"). `agent.tail` keeps the newest lines that fit in 12 KB, each cut to 400 characters. The device drops a reply it cannot hold and fails that call, instead of running out of memory.

## Methods

| Method | Params | Result |
|--------|--------|--------|
| `ping` | `{}` | `{"t":<unix ms>}` |
| `sub` | `{"topics":["stats","agents","notify"],"ms":2000}` | `{}` |
| `stats.get` | `{}` | `Stats` |
| `procs.list` | `{"sort":"cpu"\|"mem","n":20}` | `{"procs":[Proc]}` |
| `procs.kill` | `{"pid":123,"sig":"TERM"\|"KILL"}` | `{}` |
| `harness.list` | `{}` | `{"harness":[Harness]}` |
| `projects.list` | `{}` | `{"projects":[Project]}` |
| `agents.list` | `{}` | `{"agents":[Agent]}` |
| `agent.create` | `{"harness":"paseo:claude","cwd":"/p","prompt":"...","name":"","mode":""}` | `{"id":"..."}` |
| `agent.send` | `{"id":"...","text":"..."}` | `{}` |
| `agent.stop` | `{"id":"..."}` | `{}` (interrupt the current turn) |
| `agent.kill` | `{"id":"..."}` | `{}` (end the agent; Paseo agents are archived) |
| `agent.tail` | `{"id":"...","n":40}` | `{"lines":["..."]}` |
| `agent.last` | `{"id":"..."}` | `{"text":"..."}` (last assistant message) |
| `agent.speak` | `{"id":"...","voice":""}` | `{"ms":4200}` (reads the last reply aloud on the device speaker) |
| `agent.permit` | `{"id":"...","allow":true,"message":""}` | `{}` (a deny with a `message` sends that text to the agent) |
| `agent.pending` | `{"id":"..."}` | `{"tool":"Bash","text":"..."}` (what a waiting agent asks; empty when nothing waits) |
| `term.open` | `{"agent":"<id>"}` or `{"shell":true,"cwd":"~"}`, plus `"cols":40,"rows":15` | `{"tid":1}` |
| `term.input` | `{"tid":1,"d":"ls\r"}` | `{}` |
| `term.close` | `{"tid":1}` | `{}` |
| `voice.start` | `{"rate":16000}` | `{}` |
| `voice.stop` | `{}` | `{"text":"..."}` |
| `voice.cancel` | `{}` | `{}` |
| `tts.say` | `{"text":"...","voice":""}` | `{"ms":1112}`, as soon as audio starts. `ms` is the audio sent so far |
| `tts.stop` | `{}` | `{}` |
| `ota.read` | `{"off":0,"n":8192}` | `{"n":8192}` (the bytes arrive first, as one kind `0x04` frame) |
| `ota.done` | `{"ok":true,"err":"","dest":"flash"\|"sd"}` | `{}` |
| `update.check` | `{"force":false}` | `{"fw":"0.3.0","latest":"0.4.0","source":"GitHub release v0.4.0","size":1321549,"newer":true,"linkOk":true,"host":"0.4.0","hostUpdate":true,"note":""}` |
| `update.firmware` | `{}` | `{"version":"0.4.0","size":1321549}`, then an `ota.offer` event with `"asked":true` |
| `update.host` | `{}` | `{"started":true,"log":"<path>"}` (runs `thronectl update --yes` on the host; the daemon restarts) |
| `config.groups` | `{}` | `{"groups":[{"id":"general","label":"General","sub":"name, shell, stats"}]}` |
| `config.fields` | `{"group":"general"}` | `{"fields":[Field]}`. `group` is a group id or `harness:<id>` |
| `config.set` | `{"key":"name","value":"Laptop"}` | `{"restart":false}`. `env.<NAME>` keys write the env file; `"value":null` removes the entry |
| `config.add` | `{"kind":"env"\|"harness","id":"MY_KEY","value":"..."}` | `{}`. For a harness, `value` is the command |
| `config.action` | `{"key":"restart"\|"remove:<id>"}` | `{"message":"..."}`. `restart` works only when the daemon runs as a login service |

## Types

`Stats`:

```json
{
  "cpu": 23.5, "cores": [10.1, 40.2], "load": [1.2, 1.0, 0.8],
  "mem": [10240, 32768], "swap": [0, 2048],
  "disk": [412.5, 994.7], "net": [120, 30],
  "up": 86400, "procs": 512,
  "batt": {"pct": 81, "chg": true},
  "agents": {"working": 2, "idle": 3, "waiting": 1}
}
```

- `mem`, `swap`: [used, total] in MiB.
- `disk`: [used, total] in GiB for the root volume.
- `net`: [rx, tx] in KiB/s.
- `batt`: absent when the host has no battery.

`Proc`: `{"pid":42,"name":"node","cpu":12.5,"mem":1.4,"user":"dev"}`. `cpu` and `mem` are percent.

`Harness`:

```json
{"id":"direct:claude","name":"Claude Code","backend":"direct","ok":true,"color":55327,
 "modes":[{"id":"default","label":"Ask"},{"id":"bypass","label":"Bypass"}],"mode":"default"}
```

`backend` is `direct` (the daemon runs the CLI in a PTY) or `paseo` (the daemon uses the Paseo CLI). `color` is RGB565.

`Project`: `{"path":"/Users/dev/code/app","name":"app","branch":"main"}`.

`Field`: `{"key":"port","label":"Port","type":"number","value":4787,"help":"","restart":true,"options":[]}`.

- `type` is one of `bool`, `text`, `number`, `choice`, `list`, `secret`, `link`, `add`, `action`.
- `list` values are one item per line. `secret` values come back masked (`sk-...9fQa`), never in full.
- `link` opens the group named by `key`. `add` asks for an id and a value, then sends `config.add` with `kind` set to `key`.
- `restart: true` means the change applies after the daemon restarts.
- `live: true` means the value changes on its own (an install in progress), so the device polls the group every 2 s.
- `confirm` (actions only) is the text the device shows before it runs the action. Without it, the action runs at once.
- `voice.install` starts the local model download. A `notify` event tells the device when it ends.
- The daemon accepts only the keys it lists. API keys go in the env file, not in `config.json`.
- With `allowShell` set to `false`, every `config.*` write fails: a device that can change a harness command can run any program.

`Agent`:

```json
{"id":"d-3","name":"fix login","harness":"direct:claude","backend":"direct","status":"working",
 "cwd":"/Users/dev/code/app","age":125,"attn":false,"last":"Editing src/login.ts"}
```

`status` is one of `working`, `idle`, `waiting`, `exited`, `error`. `waiting` means the agent needs a permission answer. `age` is seconds since start. `last` is one short line of recent output.

## Events

| Event | Data |
|-------|------|
| `stats` | `Stats`, every `ms` after `sub` (per device, 500 ms to 1 h). Leave `stats` out of `topics` to pause it |
| `agents` | `{"agents":[Agent]}`, when the list changes |
| `notify` | `{"kind":"done"\|"attn"\|"error"\|"info","title":"...","body":"...","agent":"<id>","secs":42}`. `secs` (done only) is how long the turn ran |
| `term` | Terminal update (see below) |
| `term_exit` | `{"tid":1,"code":0}` |
| `tts` | `{"state":"start","rate":16000}` or `{"state":"end"}` |
| `ota.offer` | `{"ver":"0.2.0","size":1182944,"sha256":"<hex>","from":"<host name>","asked":false}` |

## Terminal updates

The host runs the terminal emulator. The device draws a grid of cells.

```json
{"tid":1,"full":false,"cx":3,"cy":14,"cv":true,
 "rows":[[14,[[-1,-1,0,"$ ls"],[2016,-1,1," src"]]]]}
```

- `rows`: list of `[y, spans]`. Each span is `[fg, bg, flags, text]`.
- `fg`, `bg`: RGB565, or `-1` for the theme default.
- `flags`: bit 0 bold, bit 1 underline, bit 2 italic, bit 3 inverse, bit 4 dim.
- For inverse, the device resolves `-1` to its theme colors, then swaps `fg` and `bg`.
- `text` is UTF-8. The device draws box-drawing, block and braille characters itself. A wide character is sent as one character and one space.
- `full:true` means the device must clear the grid first.
- The host sends at most 15 updates per second and only changed rows.

## Audio

- Microphone (kind `0x02`): the device sends 16 kHz PCM between `voice.start` and `voice.stop`, in chunks of 100 ms or less.
- Speaker (kind `0x03`): the host sends PCM at the rate given in the `tts` start event, in chunks of 100 ms, at real time plus 400 ms lead.

## Firmware updates

`thronectl flash` (and `thronectl update`) send an app image to connected devices. A merged flash image is cut at its first app partition, so the device always gets an app image.

1. The host sends the `ota.offer` event. The device asks the user, unless `asked` is true (the user asked for it on the device with `update.firmware`). If the user declines, the device calls `ota.done` with `ok:false`.
2. The device calls `ota.read` for each chunk, in order, with up to two calls in flight. A chunk is 16 KB or less.
3. When all bytes are in, the device checks the SHA-256, then calls `ota.done`.
4. A standalone install writes the spare app slot and restarts into it (`dest:"flash"`).
5. Under Launcher (an app partition of subtype `test` exists), the device writes `/thronectl-cardputer-adv-v<ver>.bin` on the SD card (`dest:"sd"`). Launcher installs it from there.

Firmware before 0.2.0 ignores `ota.offer`.
