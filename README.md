<h1 align="center">📱 IPHONE 1</h1>

<p align="center">
  <strong>The original iPhone. The original software. In your browser.</strong>
</p>

<p align="center">
  iPhone OS 1.0 · Build <code>1A543a</code> · iPhone1,1 / M68AP
</p>

<p align="center">
  <a href="https://2007.mobile/"><strong>Try the browser demo</strong></a>
  ·
  <a href="https://github.com/hopeugetherpes/iphone-1-emulator"><strong>Source code</strong></a>
  ·
  <a href="#deploy-to-vercel"><strong>Deploy your own</strong></a>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/iPhone_OS-1.0-111111?style=flat-square" alt="iPhone OS 1.0">
  <img src="https://img.shields.io/badge/CORE-C_%2B_WebAssembly-444444?style=flat-square" alt="C and WebAssembly core">
  <img src="https://img.shields.io/badge/ANALYTICS-NONE-706e70?style=flat-square" alt="No application analytics">
  <a href="LICENSE"><img src="https://img.shields.io/badge/LICENSE-GPL--2.0--or--later-1E6E50?style=flat-square" alt="GPL 2.0 or later"></a>
</p>

---

# 2007, running in your browser.

Slide to unlock. Open Calculator. Write a note. Save the phone and return to it later.

**iphone-1 runs Apple's original iPhone OS 1.0 on emulated first-generation iPhone hardware.**

The kernel, frameworks, SpringBoard, and applications are Apple's original software.
SpringBoard is the home-screen application. The browser displays the guest framebuffer:
the actual pixels drawn by the emulated phone. HTML and CSS provide the case and surrounding controls.

The portable C core derives from [S5LBox](https://github.com/j0shua-SYSON/S5LBox).
This project adds an original-iPhone hardware target, an OS 1.0 boot path,
a native diagnostic tool, and a WebAssembly browser interface.

**The original phone's software, running through emulated hardware.**

## Purpose

Bring the first iPhone's software into a browser where you can interact with its interface,
explore the original apps, and keep a saved phone between visits.

The phone already boots and supports several offline workflows.
Completing the remaining apps, hardware models, and performance work is still part of the project.

---

## Current status

The launch and workflow summary below is based on the code and retained test reports checked on
**October 13, 2026**. Later updates are recorded in the [engineering log](plans/execution.md).

**Key features:**

- 📱 **Original interface** — All 16 apps reached recognizable UI in the launch audit.
- 🧮 **Offline apps** — Focused browser tests cover Calculator, Notes, Calendar, and Contacts.
  Tested organizer edits survive save and reload.
- 👆 **Interactive controls** — Taps, scrolling, slide to unlock, and the physical phone buttons work
  in the browser. Broader gesture behavior still needs tests.
- ⏱️ **Clock workflows** — Focused tests cover Stopwatch timing and manual date/time changes across
  save and reload. Audible alarms remain unfinished.
- 💾 **Persistent phone** — Manual save, autosave, pause, backup export, and backup import are available.
  Tests cover selected recovery and saved-phone upgrade cases.
- 📴 **Offline visits** — **Keep offline** stores the page, emulator, and verified firmware
  for disconnected reloads.

Opening an app establishes that its interface can appear. Many original apps still cannot
complete their main tasks.

See the [app audit](plans/app-coverage.md) for tested workflows and their limits.
Some entries describe older fixtures; read their dates and stated scope alongside the
[engineering log](plans/execution.md).

---

## 📖 Usage

### Open the phone

1. Open the [browser demo](https://2007.mobile/) or your own deployment.
2. Wait for the firmware to download and pass its integrity checks.
3. Slide to unlock.
4. Tap the guest screen to open an app; drag the screen to scroll.
5. Use **Save** or export a backup when you want to keep your progress.

The default browser restores a prepared home-screen state to avoid a slow full boot.

## Controls and saved data

### Physical controls

| Control | Keyboard shortcut |
| --- | --- |
| Home | Escape or H |
| Sleep/wake | P |
| Volume up/down | + / - |
| Silent mode | M |

Volume and silent-mode controls change emulated hardware state.
Their audible effects are not yet verified. Original system sounds such as the lock click
are documented separately under [audio and video](#issues-that-still-need-fixes).

### Save, pause, and return

**Save** stores a compressed checkpoint in the browser and retains a previous generation.
A checkpoint contains the phone's memory, device state, and stored data.

Autosave runs every **30 seconds** when input is idle.
The page also tries to save when its tab becomes hidden.

**Pause** and **Resume** remain available during ordinary saves.
Saving a paused phone leaves it paused.

### Export and import

- **Export** creates a portable `.snap` backup.
- **Import** accepts a compatible snapshot, including a gzip-compressed snapshot.
- One tab owns the writable profile. Other tabs can explore the phone and export backups.
- Incompatible saves are preserved for export instead of being silently reset.

**Keep exported backups.** Browser storage can be cleared or reach its limit.

**Reset** removes the current saved state and restores a fresh phone.
Export your phone before using it. Importing an older backup also restores that backup's older state.

### Keep offline

**Keep offline** downloads the complete offline package: the page, emulator, and verified firmware.

Offline mode requires **service-worker support** and **HTTPS or localhost**.
If a cached firmware file fails its SHA-256 check, the browser tries a fresh download.
Offline package updates wait until existing phone tabs close.

This is a cached browser installation. Saving the page as an HTML file alone does not
include its emulator, firmware, and service-worker cache.

## 🛡️ Privacy

The browser loads **no analytics SDKs** and sends no visit, interaction, or lifecycle events.

Saved phone data stays in this browser unless you export a backup.
The default firmware downloads still come from
[`https://iphone.files.garden/`](https://iphone.files.garden/).

Local emulation and browser storage do not remove that initial firmware-download dependency.
Use **Keep offline** before a disconnected visit.

---

<a id="deploy-to-vercel"></a>

## ▲ Deploy to Vercel

[![Deploy with Vercel](https://vercel.com/button)](https://vercel.com/new/clone?repository-url=https%3A%2F%2Fgithub.com%2Fhopeugetherpes%2Fiphone-1-emulator&root-directory=web&project-name=iphone-1-emulator&repository-name=iphone-1-emulator)

The browser edition is a **static site**. Its compiled WebAssembly and browser assets are
already committed under `web/`.

1. Click **Deploy with Vercel**. The button selects `web` as the **Root Directory**.
2. Use the **Other** framework preset.
3. Leave the **Build Command** empty and use the root of that directory (`.`) as the output.
4. Deploy.

When importing the repository manually, select the same `web` root directory.

No application server, database, environment variable, or Emscripten build is required
for this static deployment. The default
[firmware manifest](web/manifest.json) still downloads firmware and the prepared home-screen state
from `https://iphone.files.garden/` on first use.

Rebuild the generated assets before deploying changes to the emulator or browser files;
see [local development](#run-the-browser-locally).

---

## ⚠️ Limitations

### Issues that still need fixes

1. **Speed** — Reduce app launch delays and qualify typing, scrolling, and animation.
   Settings still exceeds the two-second warm-launch target in the recorded cloud baseline.
2. **Networking** — Complete a usable guest network connection and compatible app services.
   General internet access, Maps, Mail, and modern service compatibility remain unfinished.
3. **Audio and video** — Original system sounds, including the lock click, play through the
   modeled I2S clock, FIFO, and DMA. iPod music and YouTube playback have not been verified,
   and sound stutters when the emulator runs slower than real time.
   Genuine video playback remains unfinished.
4. **Photos and Camera** — Complete photo-library navigation, camera preview, capture,
   and saved media. An isolated importer supplies images, but broader photo workflows still
   fail or lack tests.
5. **Phone and Text** — Complete simulated call and message workflows through the original apps.
   The baseband device is not ready. Real carrier service lies outside the initial product scope.
6. **Hardware and reliability** — Complete USB host transfers and the native NAND storage layout.
   Check broader gestures, preference effects, alarms, long sessions, and saved-data upgrades.
   Test actual Safari and Chrome on reference laptops.
7. **CPU accuracy** — Fix the known WebAssembly floating-point failures.
   Existing tests expose missing exception flags and incorrect rounding behavior.
   See the [floating-point failure report](https://github.com/hopeugetherpes/iphone-1-emulator/releases/tag/traces-archive)
   (`docs/traces/wasm-vfp-conformance/README.md` in the traces archive).

The [product roadmap](plans/web-iphone.md) defines the remaining targets.
The [completion checklist](plans/completion-checklist.md) explains the larger gaps.
Hardware-specific plans and reports contain the detailed failure evidence.

### Experimental app services

Separate prepared images demonstrate additional workflows:

| App | Demonstrated workflow | Limit |
| --- | --- | --- |
| Safari | Loads local pages, submits a form, and preserves a bookmark across reload | Prepared image and local services required |
| Weather and Stocks | Display fixed demo data through local services | Fixed data; prepared image and local services required |
| YouTube | Signs a local device challenge and displays one demo catalog entry | The player receives movie bytes, but playback still fails |

These experiments use the original apps and guest networking code.
**The default phone does not include these services.**
Each experiment requires its own prepared image and local services.

See the [service tools](tools/legacy_services/README.md) and
[YouTube investigation](plans/youtube-services.md).

### Performance evidence

A **September 8** cloud baseline used **Chrome 152** on a **2.90 GHz Xeon**,
with **30 warm samples per action**:

| Action | Median | p95 |
| --- | --- | --- |
| Settings | 2.318 seconds | 2.567 seconds |
| Home | 0.866 seconds | 0.868 seconds |

The p95 value describes the threshold for 95% of the samples.
These measurements apply to that fixture and cloud machine; performance on your hardware still needs testing.

See the [baseline report](https://github.com/hopeugetherpes/iphone-1-emulator/releases/tag/traces-archive)
(`docs/traces/adaptive-compiler-baseline/README.md` in the traces archive).

The default browser uses a saved home-screen snapshot and accelerated graphics.
The CPU engine executes WebAssembly code generated for selected instruction regions;
runtime checks return unsupported work to the interpreter.

Historical report availability is explained under [test reports](#reports-and-historical-evidence).

---

## 🧑‍💻 Development

### Run the browser locally

You need **Git, Node.js, npm, CMake, a C/C++ compiler, and Python**.
The native build commands below also use **Ninja**.
CI uses **Node.js 22** and **Emscripten 4.0.16**.

Clone the repository:

```sh
git clone https://github.com/hopeugetherpes/iphone-1-emulator.git
cd iphone-1-emulator
```

On Amazon Linux, the setup script installs the build dependencies and Emscripten SDK:

```sh
bash tools/setup.sh
npm run serve
```

Open **http://127.0.0.1:8080/**.

Set `PORT` to choose another server port. If you define `CONDUCTOR_PORT`,
the server uses it unless `PORT` overrides it.

With build dependencies and Emscripten already installed:

```sh
npm ci
bash tools/build-web.sh
npm run serve
```

The build script finds Emscripten on `PATH` or in `.context/emsdk`.
It writes generated files to `web/dist/` and `web/jit/` and regenerates the offline package
(`web/service-worker.js`).

These files are committed because Vercel serves `web/` as-is.
**Rebuild after changing browser files.**

### Firmware downloads

The default [manifest](web/manifest.json) downloads firmware and a saved home state
from `https://iphone.files.garden/`.

Every download is checked against its recorded **SHA-256 digest**.
The browser then restores the saved state instead of performing a full cold boot.
Source copies of these firmware objects remain in `web/firmware/`.

## Firmware and native diagnostics

### Prepare a work image

The prepared work image seeds local activation data and disables automatic locking.
It mounts the root filesystem from a **RAM disk** instead of the native NAND partitions.

These changes affect data files and boot configuration.
**Apple's executable code is not patched.**
The emulator retains the upstream [S5LBox license](third_party/S5LBox-LICENSE).

Supply the original restore package and its root-filesystem key locally:

```sh
python3 -m pip install -r tools/firmware/requirements.txt
python3 tools/firmware/extract_1a543a.py iPhone1,1_1.0_1A543a_Restore.ipsw fw/ --rootfs-key HEX
python3 tools/firmware/rootfs_work.py fw/rootfs_1A543a_hfs.img fw/work_1A543a.img \
    --fstab "/dev/md0 / hfs rw 0 1" \
    --launchd-env com.apple.SpringBoard.plist=LK_ENABLE_MBX2D=1,LK_ENABLE_OGL=0 \
    --add /var/root/Library/Lockdown/data_ark.plist=tools/firmware/data_ark_activated.plist \
    --no-autolock --activate
```

The extractor produces a kernel, device tree, and root filesystem.
The work-image tool copies the extracted image before changing its data.

These commands prepare a work image. Recreating every packaged snapshot or release artifact
requires additional steps. See the [boot and hardware log](docs/iphone1_1_os1_port.md).

### Build and test the native tool

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

### Run a full native boot

With the prepared firmware files:

```sh
mkdir -p .context
./build/tools/iphone1 --board m68 \
    --kernel fw/kernel_1A543a.macho --devicetree fw/devicetree_m68ap.bin \
    --ramdisk fw/work_1A543a.img --ram-mb 256 --debug-enabled \
    --cmdline "debug=0x8 serial=3 rd=md0 -v" \
    --steps 12000000000 --serial .context/run.log \
    --screenshot .context/run.ppm --shot-every 500000000
```

A full boot takes **billions of guest instructions**.

The native diagnostic tool supports scripted input, snapshots, screenshots, and CPU/device logs.
It does not provide a desktop phone window on the current `main` branch.

## Tests

### Unit and WebAssembly checks

After the web build:

```sh
npm run test:unit
npm run test:web
npm run test:capture
```

### Browser checks

With the server running and Chrome installed, run the relevant checks:

```sh
npm run test:browser
npm run test:controls
npm run test:persistence
npm run test:offline
npm run test:offline-update
npm run test:clock
npm run test:save-pause
npm run test:calculator
npm run test:notes
```

Browser tests default to `/usr/bin/google-chrome`.
Set `CHROME_PATH` to choose another Chrome or Chromium executable.

The background-tab clock test also needs `DISPLAY` or Xvfb.
On Amazon Linux, install Xvfb with `sudo dnf install -y xorg-x11-server-Xvfb`.

Recording tests need Playwright's encoder: `npx playwright install ffmpeg`.

### Reports and historical evidence

Keep generated reports, screenshots, recordings, and exports in ignored `.context/` paths.

Historical engineering reports formerly under `docs/traces/` were removed from repository
history to keep clones small. **This fork has not imported the `traces-archive` release.**
Archive links remain unavailable until that release is added here.

Historical browser recordings and milestone publishing files no longer belong to the branch histories.
Retained engineering reports describe their original runs; historical hashes can predate
the archival cleanup.

---

## Repository guide

| Path | Purpose |
| --- | --- |
| `core/` | Portable ARMv6 CPU, memory, boot, and hardware models |
| `web/` | Browser interface, worker, WebAssembly API, saves, and offline cache |
| `tools/iphone1.c` | Native diagnostic tool |
| `tools/firmware/` | Firmware extraction and work-image tools |
| `tools/jit/` | Generated CPU code and isolated compiler experiments |
| `tools/legacy_services/` | Local app services and prepared-image experiments |
| `plans/` | Implementation history, feature plans, and remaining work |

Read [CONTRIBUTING.md](CONTRIBUTING.md) before contributing.
See [SECURITY.md](SECURITY.md) for the security policy.

## License

**iphone-1 is free software under the GNU General Public License, version 2 or,
at your option, any later version.** See [LICENSE](LICENSE).

The portable C core derives from [S5LBox](https://github.com/j0shua-SYSON/S5LBox).
Its MIT notice is preserved in [third_party/S5LBox-LICENSE](third_party/S5LBox-LICENSE)
and in the affected source files.

The license covers **this project's source code only**.
Apple firmware, iPhone OS software, and files derived from them remain Apple's property
and are not licensed by this project.

iPhone and iPhone OS are trademarks of Apple Inc.
iphone-1 is not affiliated with or endorsed by Apple.

---

<p align="center">
  <strong>IPHONE 1</strong>
</p>

<p align="center">
  ORIGINAL SOFTWARE · EMULATED HARDWARE · LOCAL SAVES
</p>

<p align="center">
  <a href="https://2007.mobile/">TRY THE DEMO</a>
  ·
  <a href="https://github.com/hopeugetherpes/iphone-1-emulator">SOURCE</a>
  ·
  <a href="LICENSE">GPL-2.0-OR-LATER</a>
</p>
