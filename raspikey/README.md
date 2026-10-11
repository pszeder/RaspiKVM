# RaspiKVM backend and frontend

This directory retains the historical `raspikey` name. It contains the native C++ input bridge, the Bluetooth output implementation, mouse controls, the isolated TV worker, and the Vue frontend in `ui/`.

Use the [project README](../README.md) and [development guide](../docs/development.html) for the supported Windows/WSL and Raspberry Pi OS Lite Trixie workflow. Run the root `dev.ps1` commands from the checkout directory. The frontend builds in WSL; the native executable builds incrementally on the Pi.

The original cross-compilation instructions targeted Raspbian Buster and release downloads from the deleted RaspiKey repository. They remain available in the [preserved original history](https://github.com/pszeder/RaspiKVM/blob/c85b8b12bb00fe585c4bd7fafe17f886033ced9f/raspikey/README.md), with credit to George Samartzidis.

Runtime service names, on-device paths, the Bluetooth name **RaspiKey KVM**, and existing API names are retained intentionally. The current project and documentation name is **RaspiKVM**.

Third-party code keeps its original license headers. In particular, the bundled BlueZ `gdbus` sources are GPL-2.0-or-later. See the root [license discussion](../README.md#license) before redistributing builds.
