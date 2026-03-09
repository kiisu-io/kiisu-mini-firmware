# Kiisu Smol Firmware

For Kiisu Smol (Mini) V1a (prerelease) only! For Kiisu V4, see [Kiisu V4 Firmware](https://github.com/kiisu-io/kiisu-firmware)

## Create updater package for flashing via qFlipper

```shell
./fbt updater_package
```

## Flash via qFlipper

1. Download qFlipper here: https://flipperzero.one/downloads
2. Download latest firmware from Releases - you need kiisu-z-f7-update-local.tgz file
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
- [Kiisu.io website](https://kiisu.io)
- [Buy Kiisu here](https://store.rainwalker.ee/products/kiisu-v4)
- [Documentation, schematics and binaries for Kiisu V4 and Kiisu Smol (Mini) V1](https://github.com/kiisu-io/kiisu4)
- [Our Discord Community](https://discord.gg/kiisu) can help with your questions
  
- [Twoelw's GitHub](https://github.com/twoelw) with useful apps and firmware for Kiisu.

- [Cases and stuff for 3D printing on Printables](https://www.printables.com/@planmarks/collections/2364779)
- [Cases and stuff for 3D printing on Makerworld](https://makerworld.com/ru/collections/6517412-kiisu-devboard)
