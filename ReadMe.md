# Kiisu Smol Firmware

Firmware for **KIISU SMOL** V1a (prerelease) only. For KIISU V4, see [Kiisu V4 Firmware](https://github.com/kiisu-io/kiisu-firmware).

> The board was previously known as *Kiisu Mini*; the official name is now **KIISU SMOL**.

🚀 **KIISU SMOL is currently on [Indiegogo](https://www.indiegogo.com/projects/rainwalker/kiisu-smol---networking-development-board).**

## Create updater package for flashing via qFlipper

```shell
./fbt updater_package
```

## Flash via qFlipper

1. Download qFlipper here: https://flipperzero.one/downloads
2. Download latest firmware from Releases - you need kiisu-v1a-090326-update-local.tgz file
3. Connect your Kiisu board to the PC
4. Use Install from file button

## Building

Build firmware using Flipper Build Tool:

```shell
./fbt
```

## Flashing firmware using an in-circuit debugger

Connect your in-circuit debugger to your Flipper and flash firmware using Flipper Build Tool:

```shell
./fbt flash
```

## Documentation

- [Flipper Build Tool](/documentation/fbt.md) - building, flashing, and debugging Flipper software
- [Applications](/documentation/AppsOnSDCard.md), [Application Manifest](/documentation/AppManifests.md) - developing, building, deploying, and debugging Flipper applications
- [Hardware combos and Un-bricking](/documentation/KeyCombo.md) - recovering your Flipper from the most nasty situations
- [Flipper File Formats](/documentation/file_formats) - everything about how Flipper stores your data and how you can work with it
- [Universal Remotes](/documentation/UniversalRemotes.md) - contributing your infrared remote to the universal remote database
- [Firmware Roadmap](https://miro.com/app/board/uXjVO_3D6xU=/)
- And much more in the [Developer Documentation](https://developer.flipper.net/flipperzero/doxygen)

# Project structure

- `applications`        - Applications and services used in firmware
- `applications_users`  - Place for your additional applications and services
- `assets`              - Assets used by applications and services
- `documentation`       - Documentation generation system configs and input files
- `furi`                - Furi Core: OS-level primitives and helpers
- `lib`                 - Our and 3rd party libraries, drivers, tools and etc...
- `site_scons`          - Build system configuration and modules
- `scripts`             - Supplementary scripts and various python libraries
- `targets`             - Firmware targets: platform specific code

Also, see `ReadMe.md` files inside those directories for further details.

## Other Resources
- [Kiisu.io website](https://kiisu.io) · [KIISU SMOL product page](https://kiisu.io/kiisu-smol/)
- Get one: [Indiegogo campaign](https://www.indiegogo.com/projects/rainwalker/kiisu-smol---networking-development-board) · [RainWalker store](https://store.rainwalker.ee/)
- Hardware design files: [kiisu-io/kiisu-smol](https://github.com/kiisu-io/kiisu-smol) (KIISU SMOL) · [kiisu-io/kiisu4](https://github.com/kiisu-io/kiisu4) (KIISU V4)
- [Discord community](https://discord.gg/kiisu) · [Reddit r/KIISU_IO](https://www.reddit.com/r/KIISU_IO/) · [Telegram](https://t.me/kiisu_io)
- [Twoelw's GitHub](https://github.com/twoelw) — useful apps and firmware for Kiisu
- 3D-printable cases: [Printables](https://www.printables.com/@planmarks/collections/2364779) · [MakerWorld](https://makerworld.com/ru/collections/6517412-kiisu-devboard)
