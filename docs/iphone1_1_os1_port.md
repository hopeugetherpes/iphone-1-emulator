# Booting genuine iPhone OS 1.0 (1A543a) on an emulated iPhone1,1 / M68AP

Running engineering log for the iPhone1,1 target. Everything here was observed
on this emulator running Apple's unmodified 1A543a kernel, device tree and root
filesystem. No firmware, Apple binary or firmware key is stored in this
repository; the developer supplies a local IPSW.

## 1. Target

| | |
| --- | --- |
| Device | original iPhone (2007) |
| Product | `iPhone1,1` |
| Board | `M68AP` / `m68ap` |
| SoC | Samsung `S5L8900` |
| CPU | ARM1176JZF-S (ARMv6, VFPv2) |
| OS | iPhone OS 1.0, build `1A543a` ("Heavenly") |
| Kernel | `Darwin Kernel Version 9.0.0d1 ... xnu-933.0.0.178 RELEASE_ARM_S5L8900XRB` |

## 2. Architecture

```
core/           portable emulator core (no host UI, no libc beyond C11) -> WASM-friendly
  src/arm/      ARMv6 interpreter (arm_interp.c), VFP
  src/mmu.c     ARMv6 short-descriptor MMU + TLB
  src/soc/      shared S5L8900 devices: vic, gpioic, timer, clcd, spi, i2c, i2s,
                dma (pl080), nor, nand, mbx, power, uart, usbotg, storage, buttons,
                shared multitouch reports and the Z1/Z2 controllers
  src/board/    board definitions: board.c (registry), m68.c, n82.c
  src/boot/     bringup.c (iOS 3 / N82 path), bringup_os1.c (OS 1.0 path),
                dt_patch.c (device-tree patching shared by both)
  src/firmware/ 8900/IMG2/IMG3 unwrap, LZSS, Mach-O loader, kernel symbols
tools/iphone1.c host harness: argument parsing, firmware unwrapping, run loop,
                UART to stdout, PPM screenshots, tracing/diagnostics
tools/firmware/ python tooling: IPSW extraction, UDIF/vfdecrypt, HFSX reader,
                device-tree dump, work-image builder, PPM->PNG
```

The split the port introduced: everything S5L8900-generic lives in `core/src/soc`,
and per-board wiring lives behind a single `s5l_board_t` vtable
(`core/include/board.h`). Board code attaches I2C/SPI slaves and picks the NOR
base; no board `if` statements exist in SoC or boot code.

### M68 vs N82 (what actually matters for booting XNU)

| | M68 (iPhone1,1) | N82 (iPhone 3G) |
| --- | --- | --- |
| RAM base / size | `0x08000000`, 128 MiB | `0x08000000`, 128 MiB |
| NOR base | `0x24000000` | `0x00000000` (in-SoC alias used by iOS 3) |
| LCD | `lcd,merlot` on **SPI1** CS0 | `lcd,merlot` on SPI0 |
| Touch | `multi-touch,z1` on **SPI2** CS0 | MTZ2 on SPI1 CS0 |
| Audio | `audio-control,wm8758` on I2C0 `0x1a` | WM8991 |
| PMU | PCF50635 on I2C0 `0x73` | PCF50635 on I2C0 |
| Panel ID patch | none (OS 1.0 reads `_lcdPanelID: 0x000000`) | required |
| Kernel | Mach-O, `__PRELINK`, virt base `0xc0000000` | `__PRELINK_INFO`, later layout |

## 3. Memory map (as the OS 1.0 kernel uses it)

RAM `0x08000000`. The real device has 128 MiB; the current runs use
`--ram-mb 256` because the ~185 MiB root filesystem is booted as a RAM-resident
ramdisk (`rd=md0`) until NAND/disk0 is modeled. This is a deliberate deviation.

Layout printed by the harness for run52:

```
virt_base=0xc0000000 phys_base=0x08000000 ram_size=0x10000000
kernel 0x08000000..0x085c4b80  entry_pa=0x0805f7f0 (va 0xc005f7f0)
devicetree_pa=0x085c5000 size=0x9000    boot_args_pa=0x085ce000
ramdisk_pa=0x085d0000 size=0x0b8a5000
framebuffer_pa=0x13e78000 vram=0x00960000  top_of_kernel_data_pa=0x147d8000
```

MMIO (from the M68 device tree, `/arm-io` ranges):

| base | size | device |
| --- | --- | --- |
| `0x00100000` | | clock/reset (MIU) second range |
| `0x00200000` | | DMA0 (PL080) |
| `0x00400000` | | USB OTG |
| `0x00900000` | `0x1000` | CLCD (`aliases/screen` -> `/arm-io/lcd`) |
| `0x00a00000` | | NAND / FMC |
| `0x00c00000` | | AES |
| `0x00d00000` | | SDIO |
| `0x00e00000` | `0x2000` | PL192 VIC |
| `0x00e02000` | | edge interrupt controller |
| `0x01900000` | | DMA1 |
| `0x01a00000` | | power controller |
| `0x03000000` | `0x1000000` | MBX (2D/3D) |
| `0x04300000` | | SPI0 |
| `0x04500000` | | clock controller |
| `0x04600000` | | I2C0 |
| `0x04900000` | | I2C1 |
| `0x04a00000` | | I2S0 |
| `0x04c00000`/`04c04000`/`04c0c000`/`04c10000` | | UART0/1/3/4 |
| `0x04e00000` | | SPI1 |
| `0x05000000` | | PKE |
| `0x05200000` | | SPI2 (runtime physical `0x3d200000`) |
| `0x06200000` | | timer |
| `0x06300000` | | watchdog |
| `0x06400000` | | GPIO |
| `0x06500000` | | chip ID |
| `0x1c000000` | `0x100000` | NOR (device-tree view; physical base `0x24000000`) |

NOR sub-ranges: `diagnostic-data` `0x6000`, `raw-device` `0x8000..0xfc000`,
`nvram` `0xfc000` and `0xfe000` (two 8 KiB copies).

## 4. Firmware formats in 1A543a

1A543a predates IMG3. Components are `8900` containers:

```
0x00 '8900'   0x07 format byte   0x0c payload length (LE u32)   0x800 payload
```

For this build the payloads are **plaintext** (no AES needed): either an IMG2
(`2gmI`, payload at `+0x400`) or a bare `complzss` blob. `tools/firmware/unwrap8900.py`
and the harness both accept 8900/IMG2/`complzss`/raw.

The root filesystem is inside the IPSW's DMG (UDIF, vfdecrypt key from the
build's key set, supplied by the developer) and is **HFSX** (`HX`, version 5,
4 KiB blocks, 47269 blocks, not journaled). `tools/firmware/extract_1a543a.py`
drives IPSW -> kernel + device tree + rootfs image; keys are never hardcoded.

## 5. Boot handoff (`core/src/boot/bringup_os1.c`)

SecureROM -> LLB -> iBoot is skipped; the harness constructs XNU's entry state
directly. That is much cheaper than emulating the boot chain and loses nothing
for this milestone (the kernel only consumes boot args + device tree).

Differences from the existing iOS 3 path that forced a separate file:

* OS 1.0's `__HIB` segment starts exactly at the virtual base `0xc0000000`
  (the iOS 3 path rejects that).
* Prelink segment is `__PRELINK`, not `__PRELINK_INFO`.
* No N82 panel-ID device-tree patches, no iOS 3 SVC memory-disk bridge.

Placement rules: device tree 4 KiB aligned, one 4 KiB boot-args page, ramdisk
64 KiB aligned, framebuffer pool 16 KiB aligned, top-of-kernel-data rounded to
16 KiB with >= 24 MiB free above it. Device-tree patches applied: CPU/root
frequencies, `/memory/reg`, `/vram/reg`, `/chosen/debug-enabled`,
`/chosen/memory-map/RAMDisk`. Boot args are revision 1 / version 1 with a
320x480x32 framebuffer description and the command line (<= 255 bytes). Entry:
`r15 = entry_pa`, `r0 = boot_args_pa`.

## 6. Boot progress reached

Command used for the current best run (`--unmatch` explained in §8):

```
iphone1 --board m68 --kernel kernel_1A543a.macho --devicetree devicetree_m68ap.bin \
        --ramdisk work_1A543a.img --ram-mb 256 --debug-enabled \
        --cmdline "debug=0x8 serial=3 rd=md0 -v" \
        --unmatch arm-io/usb-otg --unmatch arm-io/spi2/multi-touch \
        --steps 1500000000 --serial run.log --screenshot run.ppm
```

Kernel log highlights (verbatim):

```
Darwin Kernel Version 9.0.0d1: Tue May 22 21:15:55 PDT 2007; root:xnu-933.0.0.178.obj~3/RELEASE_ARM_S5L8900XRB
vm_page_bootstrap: 14111 free pages and 51425 wired pages
70 prelinked modules
AppleS5L8900XIO::start: chip-revision: EVT0
AppleARMPL192VIC::start: _vicBaseAddress = 0xedc14000
AppleS5L8900XClockController: Dynamic Performance State Management Enabled with max state 3
AppleMBX: Detected mbx,s5l8900x.  SupportsWriteCombining: true
AppleImage2NORAccess::start(raw-device) <1>
ApplePCF50635PMU::start: pmu _pmuIICNub: 0xccbec800
BSD root: md0, major 2, minor 0
AppleMerlotLCD: _lcdPanelID: 0x000000
Jettisoning kernel linker.
AppleMBX: Added swap device: AppleH1CLCD  id: cc995200
/dev/md0 on / (hfs, local, noatime)
```

Userspace: genuine `launchd` runs, `fsck_hfs` validates the volume ("The volume
Heavenly1A543a.UserBundle appears to be OK"), daemons start (`configd`,
`notifyd`, `syslogd`, `BTServer`, `lockdownd`, `CommCenter`), and
`_execve` tracing shows Apple's own binaries executing, including
`/System/Library/CoreServices/SpringBoard.app/SpringBoard`.

SpringBoard loads its real frameworks (UIKit, CoreGraphics, Foundation,
LayerKit, IOMobileFramebuffer, CoreSurface, MBX2D, MBXConnect, OpenGLES,
MultitouchSupport), reads its genuine artwork/fonts/app metadata, and **renders
into the framebuffer**: the captured 320x480 PPM contains ~44k distinct colours
and shows the authentic 2007 "Activate iPhone / connect to iTunes" lock screen
with the Earth wallpaper, the "Searching..." status bar and the emergency-call
slider.

That is the first-pixels milestone: genuine SpringBoard drawing on emulated
hardware.

### Home screen

With the activation state seeded into the work image (§9) and a 12G-instruction
run (`--shot-every 500000000` writes a PPM every 500M instructions):

* ~0.5–5G: kernel text, then black while SpringBoard starts and waits for
  `CommCenter`/`lockdownd` (SpringBoard's main thread blocks ~4.5G instructions
  in a Mach receive on the CommCenter registration reply);
* 5.5G: **the genuine June-2007 home screen** — status bar with time and
  battery, the 12 icons (Text, Calendar, Photos, Camera, YouTube, Stocks, Maps,
  Weather, Clock, Calculator, Notes, Settings) and the Phone/Mail/Safari/iPod
  dock;
* 6.5G: a "Repair Needed — iPhone cannot make or receive calls" alert on top of
  the home screen (CommCenter has no baseband — expected, dismissable once touch
  exists);
* later the display is turned off (`AppleMerlotLCD::_lcdEnable: enable: 0`):
  SpringBoard's idle auto-lock, since there is no input yet.

SpringBoard's main thread then sits in a healthy CFRunLoop (~2k syscalls per
guest-second, mostly `mach_msg` with CommCenter). The run takes ~5.5 min of host
time for 12G instructions.

## 7. Hardware behaviour implemented for this target

* **NOR + CHRP NVRAM.** M68 NOR sits at physical `0x24000000`. xnu-933's
  `IODTNVRAM::initNVRAMImage` walks partitions by their length field and has no
  `length < 16` guard, so a blank NVRAM hangs it. `m68.c` programs a valid
  `common` partition (with the CHRP checksum) into both 8 KiB copies, which is
  what a real unit ships with.
* **Timer/timebase.** OS 1.0 programs timer4 differently from iOS 3. The tick
  model now reloads from the period register when the value register is zero
  and computes the post-expiry phase, and the machine converts CPU cycles to
  timebase ticks with an accumulator (`412 MHz` CPU, `6 MHz` timebase). Before
  this, the 100 Hz `hertz_tick` fired ~5x in 114M instructions instead of ~27x
  and `IODelay` wakeups arrived millions of instructions late, which broke
  driver-registration ordering.
* **MBX.** OS 1.0 writes 0 (as well as 1) to the TA context-reset register and
  polls the status bit; the model now completes for 0 and 1.
* **ARM exception state.** `MSR CPSR` in exception handlers no longer allows the
  T bit (or bit 24) to be written, matching ARM1176 behaviour; without this the
  kernel took a wild data abort during symbol lookup.
* **Merlot LCD on SPI1**, PCF50635 PMU, WM8758 (register writes are NAKed and
  the driver tolerates it), CLCD scanout to the host framebuffer.
* **Z1 multitouch on SPI2 CS0.** The model implements the two-stage bootloader:
  `c2` A-speed upload, `c4` execute, `c3` B-speed stream, `05` checksum, then
  `c4` firmware transition. Firmware mode uses full-duplex `aa` replies for
  `d0` interface version, `8f` report information, `82` report data and `92`
  set-report. Result-length reads use `aa L_hi L_lo checksum_hi checksum_lo`;
  result-data reads return a `0xcc` path frame followed by its big-endian
  payload checksum. Reports `d1`, `d3`, `d7`, `d9`, `d0`, `a1` and `ab` share
  MTZ2's 15-row, 10-column, 4800x7200 geometry. Attention is GPIOIC line 163.

## 8. Workarounds currently in force (each is a substitution for hardware we
have not modeled yet, not a firmware patch)

1. `--unmatch arm-io/usb-otg` — flips one byte of the node's `compatible` string
   in the *loaded copy* of the device tree so IOKit does not match the USB
   drivers. Reason: `AppleS5L8900XUSBWrangler::phyRegistered` dereferences its
   notifier pointer, and with our (correct) synchronous registration the
   callback runs before the pointer is stored, i.e. real hardware/timing gives
   the PHY a slower registration path. USB is not needed for SpringBoard.
   Replaceable by modeling USB OTG PHY registration timing.
2. Single-ramdisk root. The stock `/etc/fstab` mounts `/dev/disk0s1` and
   `/dev/disk0s2`; we boot the rootfs as `md0` with no NAND partitions, so
   `launchd` aborts in `fwexec(mount_tool)`. `tools/firmware/rootfs_work.py`
   builds a *work image* (a copy; the extracted firmware image is never
   modified) whose `/etc/fstab` is one line `/dev/md0 / hfs rw 0 1`, and which
   sets `LK_ENABLE_MBX2D=0` / `LK_ENABLE_OGL=0` in SpringBoard's launchd plist.
   Replaceable by modeling NAND/disk0 partitions.

No Apple binary is modified anywhere in this pipeline.

## 9. Activation (solved by seeding lockdownd's cache)

Unseeded, SpringBoard shows "Activate iPhone" because `lockdownd` reports the device as
unactivated. Disassembly of the genuine `lockdownd` shows the state comes from
its "data ark":

* it reads `/var/root/Library/Lockdown/data_ark.plist` (absent on a fresh
  filesystem) and `/System/Library/Lockdown/Checkpoint.xml`;
* with no cached state it logs "No cached activation state" and sets
  `Unactivated`;
* the activation-record check (at `0x9de0` in the binary) verifies
  `DeviceCertificate`/`AccountToken`/`AccountTokenSignature` against
  `/System/Library/Lockdown/iPhoneActivation.pem` and `iPhoneDeviceCA.pem`
  (or the `Factory*` pair) — i.e. iTunes' signature, which we cannot produce.

The cheap, binary-preserving route is therefore to seed a cached *Activated*
state in the data ark of the work image. `rootfs_work.py --add
/var/root/Library/Lockdown/data_ark.plist=data_ark.plist` creates the folder
and file in the HFSX catalog (folder/file + thread records, allocation bitmap,
volume counters, both volume headers). The seed is a flat plist:

```xml
<dict>
  <key>com.apple.mobile.lockdown_cache-ActivationState</key>
  <string>Activated</string>
</dict>
```

`--swi-log` confirms `lockdownd` opens `/var/root//Library/Lockdown/data_ark.plist`,
and with it SpringBoard proceeds to the home screen (§6). Earlier seeded runs
looked like a regression (kernel text only) purely because they were stopped
before the ~5G-instruction mark where SpringBoard wakes up.

No activation record, certificate or signature is involved; the data ark is
per-device state, not an Apple binary.

## 10. Diagnostics and input in the harness

`--trace-devices`, `--break-pc`, `--watch-pc`, `--trace-from/--trace-last`
(cycle-based, 2M-entry ring of `pc/cpsr/r0-r3/lr/sp`), `--exec-pc` (prints every
`_execve` path), `--swi-log` (Darwin/Mach syscall log with decoded paths),
`--dump-va`, `--unmatch`, `--break-swi SYSCALL TTBR0`, `--shot-every N`
(periodic PPMs), and the `--serial` file prefixes every line with the guest
instruction count so kernel messages line up with the syscall log. `tools/firmware/ksym.py` and the local `tsym.py`
annotate traces with kernel symbols.

Repeatable host input is available without a UI frontend:

```text
--at STEP tap X Y
--at STEP touch down|move|up X Y
--at STEP button NAME down|up
--at STEP press NAME
```

`NAME` is one of `hold`, `menu`, `volup`, `voldown` and `ringerab`. Every
injected event is printed to stderr. `tap` sends contact 1 down and schedules
an automatic lift 200000 instructions later; `press` releases the button
300000 instructions later. Touch events call the machine-level API and use the
same coordinate conversion as MTZ2.

Snapshots preserve the guest CPU instruction count and device state:

```text
--save-at 5800000000 /home/ubuntu/firmware/home.snap
--load /home/ubuntu/firmware/home.snap --at 6000000000 tap 48 60 --steps 6500000000
```

Loading resumes absolute instruction counting from the saved CPU cycle count;
`--steps` is an absolute stop count, not an additional run length. Snapshot
files and firmware images belong outside this repository.

## 11. Next steps

1. Input: Home/hold buttons through the M68 buttons GPIOs (dismiss the Repair
   alert, wake the display), and Z1 multitouch scripted events.
2. Speed: 12G instructions take ~5.5 min; an interpreter fast path or a JIT for
   the boot-to-home-screen path is needed before interactive use (and the WASM
   port).
3. Model USB OTG PHY registration ordering -> remove workaround 1; model NAND
   partitions -> remove workaround 3.
4. Then: application launching, Settings, MobileSafari, stability; only after
   that, the Emscripten/WASM port (explicitly out of scope for now).

Nothing found so far suggests the goal is infeasible: the kernel, drivers,
userspace and SpringBoard all run unmodified, and the remaining blockers are
ordinary missing-device / missing-state problems.
