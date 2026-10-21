# thronectl wire protocol v1

The device (Cardputer ADV) is the client. The host daemon (`thronectl daemon`) is the server.

## Discovery

- UDP port `4787`.
- The device sends the ASCII datagram `THRONE?1` to `255.255.255.255:4787`.
- Each daemon replies to the sender with one JSON datagram:

```json
{"t":"throne","v":1,"id":"9f2c1a0b4d5e6f70","name":"Ryan's MacBook","port":4787,"os":"darwin"}
```

`id` is the host ID (see Identity). The device also accepts a manual `host:port` entry.

## Framing

- TCP port `4787` (same number as discovery).
- Each frame is a 4-byte big-endian length, then the payload. The maximum payload is 65536 bytes.
- Handshake frames are plaintext UTF-8 JSON.
- All frames after the handshake are encrypted (see Encryption).

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
{"t":"hello","v":1,"id":"<host id>","name":"Ryan's MacBook","pk":"<32B>","epk":"<32B>","n":"<hn, 16B>"}
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

If a frame does not decrypt, the receiver closes the connection.

## Authorization

The first encrypted message from the host is `auth`.

- Trusted device, and `known` was true: `{"t":"auth","state":"ok","host":Host}`.
- Otherwise: `{"t":"auth","state":"pair","code":"482913"}`. The device shows the code. The host asks the user to compare the code and answer yes or no (`thronectl pair`, or the daemon terminal).
  - Yes: the host stores the device public key. It sends `{"t":"auth","state":"ok","host":Host}`. The device stores `{id, name, pk, addr, port}` in NVS.
  - No, or no answer in 120 s: `{"t":"auth","state":"denied"}`, then the host closes the connection.

The device sends no requests before `state:"ok"`.

`Host`:

```json
{"id":"9f2c...","name":"Ryan's MacBook","hostname":"rmbp","os":"darwin","arch":"arm64","user":"ryan","ver":"0.1.0","paseo":true}
```

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
| `agent.permit` | `{"id":"...","allow":true}` | `{}` |
| `term.open` | `{"agent":"<id>"}` or `{"shell":true,"cwd":"~"}`, plus `"cols":40,"rows":15` | `{"tid":1}` |
| `term.input` | `{"tid":1,"d":"ls\r"}` | `{}` |
| `term.close` | `{"tid":1}` | `{}` |
| `voice.start` | `{"rate":16000}` | `{}` |
| `voice.stop` | `{}` | `{"text":"..."}` |
| `voice.cancel` | `{}` | `{}` |
| `tts.say` | `{"text":"...","voice":""}` | `{}` |
| `tts.stop` | `{}` | `{}` |

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

`Proc`: `{"pid":42,"name":"node","cpu":12.5,"mem":1.4,"user":"ryan"}`. `cpu` and `mem` are percent.

`Harness`:

```json
{"id":"direct:claude","name":"Claude Code","backend":"direct","ok":true,"color":55327,
 "modes":[{"id":"default","label":"Ask"},{"id":"bypass","label":"Bypass"}],"mode":"default"}
```

`backend` is `direct` (the daemon runs the CLI in a PTY) or `paseo` (the daemon uses the Paseo CLI). `color` is RGB565.

`Project`: `{"path":"/Users/ryan/code/app","name":"app","branch":"main"}`.

`Agent`:

```json
{"id":"d-3","name":"fix login","harness":"direct:claude","backend":"direct","status":"working",
 "cwd":"/Users/ryan/code/app","age":125,"attn":false,"last":"Editing src/login.ts"}
```

`status` is one of `working`, `idle`, `waiting`, `exited`, `error`. `waiting` means the agent needs a permission answer. `age` is seconds since start. `last` is one short line of recent output.

## Events

| Event | Data |
|-------|------|
| `stats` | `Stats`, every `ms` after `sub` |
| `agents` | `{"agents":[Agent]}`, when the list changes |
| `notify` | `{"kind":"done"\|"attn"\|"error"\|"info","title":"...","body":"...","agent":"<id>"}` |
| `term` | Terminal update (see below) |
| `term_exit` | `{"tid":1,"code":0}` |
| `tts` | `{"state":"start","rate":16000}` or `{"state":"end"}` |

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
