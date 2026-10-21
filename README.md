# thronectl

Run your coding agents from a keyboard that fits in your pocket.

thronectl has two parts:

- **Firmware** for the M5Stack **Cardputer ADV** (StampS3A, ESP32-S3FN8, 1.14" ST7789V2 IPS).
- **A host daemon** for macOS and Linux. It runs your agent CLIs in terminals on the host and talks to the device over an encrypted Wi-Fi link.

From the device you can start, watch, prompt, approve and stop agents (Claude Code, Codex, Pi, Cursor Agent, Antigravity, Gemini CLI, OpenCode, Amp and more). You can also open a real shell on the host, see CPU, memory, disk and network levels, kill processes, and talk to your agents with push-to-talk. Replies are read back on the device speaker.

| | | |
|---|---|---|
| ![Home](docs/img/home.png) | ![Agents](docs/img/agents.png) | ![Agent](docs/img/agent_detail.png) |
| Dashboard: host load, agent counts, alerts | Every agent, with live status | Output tail, approve or deny, prompt |
| ![New agent](docs/img/new_4_prompt.png) | ![Terminal](docs/img/terminal.png) | ![Voice](docs/img/voice_rec.png) |
| Launch wizard: harness, folder, mode, prompt | Full terminal: shell or attach to an agent | Hold G0 and talk |
| ![Pairing](docs/img/pairing.png) | ![Processes](docs/img/procs.png) | ![Settings](docs/img/settings_display.png) |
| Pairing code, confirmed on the host | Top processes, terminate or kill | 12 themes, 9 accent colors |

## Quick start

### 1. Install the daemon (macOS or Linux)

Needs Node.js 20.10 or newer.

```sh
git clone https://github.com/yannelli/thronectl.git
cd thronectl/daemon
npm install
npm run build
npm link            # puts `thronectl` on your PATH
thronectl doctor    # shows which agent CLIs, Paseo and voice providers it found
thronectl install   # start at login (launchd on macOS, systemd --user on Linux)
```

To run it in the foreground instead: `thronectl daemon`. Pairing requests are then asked in that terminal.

### 2. Flash the Cardputer ADV

Get `thronectl-cardputer-adv-v<version>.bin` from the [releases](https://github.com/yannelli/thronectl/releases). It is one merged image for offset `0x0`.

- **[Launcher](https://github.com/bmorcelli/Launcher):** copy the `.bin` to the SD card, then in Launcher pick SD, select the file and choose Install. Launcher reads the partition table in the image and installs only the app. Factory reset in thronectl clears only its own data, so Launcher keeps its settings.
- **Browser:** open <https://espressif.github.io/esptool-js/>, connect, set the flash address to `0x0`, pick the `.bin`, then press Program.
- **esptool:**

  ```sh
  pip install esptool
  esptool.py --chip esp32s3 --baud 1500000 write_flash 0x0 thronectl-cardputer-adv-v0.1.2.bin
  ```

- **From source:** `cd firmware && pio run -t upload`

If the port does not show up, hold **G0** while you plug in USB. This puts the chip in download mode.

### 3. Pair

1. On the device, join Wi-Fi (the Wi-Fi screen opens on first boot).
2. The Hosts screen lists daemons on your network. Select yours and press **Enter**. To add a host by IP, press **a**.
3. The device shows a 6-digit code. On the computer run:

   ```sh
   thronectl pair
   ```

   Check that the codes match and answer **y**.
4. The device remembers the host and reconnects by itself. The host remembers the device. Later connections skip pairing.

To list or remove paired devices, use `thronectl devices` and `thronectl devices rm <name|id>`.

## Using it

The Cardputer has no arrow keys. In menus, the keys printed with arrows work without **fn**: `;` up, `.` down, `,` left, `/` right. `` ` `` or Backspace goes back. In text fields and the terminal, hold **fn** for arrows. **fn+`** is Esc.

### Everywhere

| Key | Action |
|-----|--------|
| **G0** (hold) | Talk. Release to send. "Tap to toggle" mode is in Settings > Voice |
| `` ` `` / Backspace | Back |
| Opt+h | Home |
| Opt+a | Agents |
| Opt+n | New agent |
| Opt+t | Shell on the host |
| Opt+s | Settings |
| Opt+m | Mute or unmute |
| Opt+Enter | Go-to menu |

### Home

`a` agents, `n` new agent, `t` shell, `p` processes, `l` notifications, `h` hosts, `w` Wi-Fi, `s` settings, `m` for the menu, `c` connect.

### Agents

| Key | Action |
|-----|--------|
| Enter | Open the agent |
| `p` | Send a prompt |
| `y` / `d` | Allow or deny a pending permission |
| `t` | Open its terminal (attach) |
| `x` | Interrupt the current turn |
| `k` | End the agent (Paseo agents are archived) |
| `v` | Read the last reply aloud |
| `l` | Show the last reply (in the agent view) |
| `f` | Filter: all, active, waiting |
| `n` | New agent |

Hold **G0** on an agent to speak a prompt to it. On any other screen, speech starts a new agent with your words as the prompt. If you have launched an agent before, the wizard reuses your last choices and goes straight to the prompt step. Press Enter to launch.

### New agent wizard

Harness, then folder, then mode, then prompt. Enter goes forward and `` ` `` goes back. In the folder step, `c` types a path. In the prompt step, Tab sets a name and Shift+Enter adds a new line.

### Terminal

All keys go to the host. Opt+q closes the terminal.

| Key | Sends |
|-----|-------|
| fn + `;` `.` `,` `/` | Arrow keys |
| fn + Shift + up / down | Page up / page down |
| fn + Shift + left / right | Home / End |
| fn + `` ` `` | Esc |
| fn + 1 to 0, `-`, `=` | F1 to F12 |
| Ctrl + key | Control characters (Ctrl+C, Ctrl+D) |
| Alt + key | Meta (Esc prefix) |
| Tab / Shift+Tab | Tab / back-tab |

Two font sizes: 40x15 (default) and 60x20 (Settings > Display > Terminal font). The host runs the terminal emulator. The device draws box, block and braille characters itself, so agent TUIs look right.

### Other screens

- **Processes:** `s` sort by CPU or memory, `k` SIGTERM, `K` SIGKILL.
- **Hosts:** Enter connect or pair, `d` default host, `x` forget, `a` add by address, `r` rescan, `o` go offline.
- **Wi-Fi:** Enter join, `r` scan, `h` hidden network, `x` forget, `d` disconnect. Up to 5 networks are saved and tried in order.

## Settings on the device

| Group | Settings |
|-------|----------|
| Display | Theme (Throne, Midnight, Tokyo Night, Catppuccin, Dracula, Nord, Gruvbox, Solarized, Synthwave, Matrix, Amber CRT, Paper), accent color, brightness, dim after, screen off after, flip screen, terminal font, 24-hour clock, battery percent, animations, boot splash, toast seconds |
| Sound | Volume, key clicks, alert sounds, startup chime, wake on alerts, play all chimes |
| Voice | Read replies aloud, push to talk (hold or tap), mic gain, send without review, host voice name, test speaker |
| LED | Off, agent status (pulses while agents work, blinks when one waits), alerts only, LED brightness |
| Agents | Confirm before ending, open terminal on launch, show exited agents, stats refresh, default harness, clear notifications |
| Network | Wi-Fi networks, hosts and pairing, auto-connect, device name, time zone (or from host), disconnect, reconnect |
| Keyboard and power | Key repeat delay and rate, CPU speed, power off |
| Device | About (firmware, heap, battery, IP, MAC, host info, latency), reboot, factory reset |

## Agents and harnesses

The daemon finds agent CLIs on your login shell `PATH`. It runs each one in a pseudo-terminal, so you can attach from the device and see the real TUI.

| Harness | Binary | Modes |
|---------|--------|-------|
| Claude Code | `claude` | Ask, Accept edits, Plan, Bypass |
| Codex | `codex` | Ask, Full auto, YOLO |
| Pi | `pi` | Ask |
| Cursor | `cursor-agent` or `agent` | Ask, Force |
| Antigravity | `agy` or `antigravity` | Ask, Auto, Plan, Bypass |
| Gemini CLI | `gemini` | Ask, Auto edit, YOLO |
| OpenCode | `opencode` | Ask, Auto |
| Amp | `amp` | Ask |
| Mistral Vibe | `vibe` | Ask, Auto approve |
| Junie | `junie` | Ask, Brave, Plan |
| Grok, Kiro, Amazon Q | `grok`, `kiro-cli`, `q` | Ask |

Agent status comes from the terminal output: **working** while output changes, **waiting** when a permission prompt is on screen, **idle** when it goes quiet, **exited** when the process ends. The device gets a notification for each change. It chimes, flashes the LED, and can read the reply aloud.

**Paseo:** if the [`paseo`](https://paseo.sh) CLI is installed, its providers appear as `paseo:<provider>` harnesses, and agents that Paseo manages show up next to the direct ones. Create, prompt, approve, interrupt, archive and attach all go through `paseo`.

Override or add a harness in the config:

```json
{ "harness": {
    "claude": { "args": ["--model", "opus"] },
    "aider":  { "bin": "aider", "name": "Aider" },
    "gemini": { "disabled": true } } }
```

## Voice

Hold **G0**, talk, release. The device streams 16 kHz audio to the daemon, which transcribes it. Spoken replies come back as audio on the device speaker.

Put API keys in `~/.config/thronectl/env` (one `KEY=value` per line). The daemon loads this file at start.

**Speech to text**, first match wins:

1. `OPENAI_API_KEY`: `gpt-4o-mini-transcribe`
2. `GROQ_API_KEY`: `whisper-large-v3-turbo`
3. A local command, for example whisper.cpp:

   ```sh
   thronectl config set voice.stt.command "whisper-cli -m ~/models/ggml-base.en.bin -nt -f {wav}"
   ```

**Text to speech**, first match wins: macOS `say`, OpenAI, ElevenLabs (with `voice.tts.voice` set), Piper (with `voice.tts.model` set), `espeak-ng`, or `voice.tts.command` (`{text}` and optional `{out}` placeholders). Set a voice per device in Settings > Voice > Voice name.

Push a message from scripts:

```sh
thronectl notify "Deploy finished" "prod is green" --kind done
thronectl say "Build failed on main"
```

## Configuration

`~/.config/thronectl/config.json` (or `$THRONECTL_HOME/config.json`). `thronectl config` prints it. `thronectl config get <key>` and `thronectl config set <key> <value>` read and change one value, and `set` tells a running daemon to reload. Restart the daemon after you change `port` or `bind`.

| Key | Default | Meaning |
|-----|---------|---------|
| `name` | host name | Name shown on the device |
| `port` | `4787` | TCP and UDP discovery port |
| `bind` | `0.0.0.0` | Listen address. Use a LAN or Tailscale IP to limit exposure |
| `discovery` | `true` | Answer device discovery broadcasts |
| `allowShell` | `true` | Allow shell terminals from the device |
| `shell` | `$SHELL` | Shell for the device terminal |
| `projectRoots` | `~/workspace`, `~/code`, `~/Developer`, ... | Folders scanned for git repos |
| `projects` | `[]` | Extra folders to pin in the project list |
| `paseo.enabled` | `"auto"` | Use the Paseo CLI when it is installed |
| `voice.stt.provider` | `"auto"` | `openai`, `groq`, `command`, `off` |
| `voice.tts.provider` | `"auto"` | `say`, `espeak`, `piper`, `openai`, `elevenlabs`, `command`, `off` |
| `voice.tts.maxChars` | `600` | Longest reply that is read aloud |
| `notify.desktop` | `true` | Desktop notification for pairing requests |
| `statsMs` | `2000` | Default stats interval |

## Security

- Each side has an X25519 key. Every connection does a static and an ephemeral key exchange. The device commits to its nonce before it sees the host's, so a man in the middle cannot choose keys that give a matching code.
- Both sides compute the 6-digit pairing code from the handshake transcript. You compare the codes and answer on the host. The device refuses to continue if the host's code differs from its own.
- After the handshake, every frame uses AES-256-GCM with a per-direction counter nonce. A frame that fails to decrypt closes the connection.
- The device pins the host key. If the key changes, it stops and asks you to forget the host and pair again.
- A paired device can run commands as your user. To turn off shell terminals, set `allowShell` to `false`. To limit the listen address, set `bind`.

The wire format is in [docs/protocol.md](docs/protocol.md).

## Development

```
daemon/     TypeScript host daemon and CLI
firmware/   PlatformIO project for the Cardputer ADV (Arduino, M5Unified, M5GFX)
docs/       Protocol spec and screenshots
```

- Daemon tests: `cd daemon && npm test`. They include an end-to-end run with a simulated device.
- Simulated device REPL: `thronectl sim`.
- Firmware build: `cd firmware && pio run`. To make the release image: `tools/merge.sh`.
- Screen snapshots without hardware: `firmware/tools/snapshot/build.sh`, then run the binary it prints with an output folder. It renders every screen to PNG with mock data.
- Key fuzzer: `firmware/tools/keyfuzz/run.sh [steps] [seed]` runs the real app loop with random keys, link and Wi-Fi changes under AddressSanitizer. It models the device clock (handlers run after the loop reads `millis()`) and fails if a key press leaves the screen off.
- Interop check: `firmware/tools/linkcheck/run.sh` builds the firmware link and crypto code for the host. It pairs with a throwaway daemon and runs the RPC methods. It needs mbedTLS 2.28 (`libmbedtls-dev`, or set `MBEDTLS_ROOT`).

## Hardware notes

- Cardputer ADV: ESP32-S3FN8 (8 MB flash, no PSRAM), 240x135 ST7789V2, TCA8418 keyboard, ES8311 codec with speaker and mic, RGB LED on GPIO 21, G0 button.
- The speaker and the mic share one I2S bus, so playback stops while you talk.
- The firmware runs one loop. The screen redraws at 30 fps while you use it and slows down when idle to save battery.
