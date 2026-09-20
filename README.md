# OpenWrt 19.07.10 Modernized

A modernized build of **OpenWrt 19.07.10** adapted for building on modern Linux hosts and contemporary GCC/Python toolchains.

This repository is based on the original OpenWrt 19.07.10 source tree and contains compatibility fixes required to build this legacy release with modern development environments.

The goal is to keep the original OpenWrt 19.07.10 platform usable while minimizing changes to the original OpenWrt architecture and target-side behavior.

> **Status:** Successfully tested with a full OpenWrt 19.07.10 build for the **Linksys WRT160NL**.

## Why this repository exists

OpenWrt 19.07.10 was released for much older host environments. Building it on a current Linux distribution can fail because of changes in:

* GCC diagnostics and language defaults
* newer C/C++ compiler behavior
* Python 3
* modern CMake
* modern system headers
* stricter compiler warnings
* changes in bundled third-party build tools

This repository provides compatibility fixes for these problems while retaining the original OpenWrt 19.07.10 base.

## Base

* **OpenWrt:** 19.07.10
* **Linux kernel:** 4.14.275
* **Target:** Atheros AR7xxx/AR9xxx
* **Subtarget:** Generic
* **Architecture:** MIPS 24Kc
* **libc:** musl
* **Test device:** Linksys WRT160NL

## Modernization changes

### GCC 15/16 compatibility

Several legacy components require adjustments for current GCC versions.

Changes include:

* compatibility with GCC 16 `nodiscard` handling in `m4`
* fixes for legacy C code relying on implicit declarations
* compatibility with stricter compiler diagnostics
* adjustments for old C/C++ code rejected by modern compilers

### Python 3 build support

Legacy OpenWrt build tools contain Python 2-era assumptions.

The SCons Python wrapper was updated to use Python 3, allowing the build system to operate on modern distributions where Python 2 is no longer available.

### pkg-config

The bundled `pkg-config` build was adjusted to use a compatible GNU C language standard with modern GCC.

### e2fsprogs

Host compilation was adjusted for compatibility with modern GCC and current C language defaults.

### libelf

The host build was adjusted to correctly pass modern compiler flags and tolerate legacy implicit declarations that would otherwise cause compilation failures.

### CMake

OpenWrt 19.07.10 contains an old CMake release.

Compatibility patches were added for modern host compilers, including:

* disabling the problematic GHS bootstrap path
* fixing missing `cstdint` usage in the worker pool implementation

### GMP

The host build configuration was adjusted for compatibility with modern host toolchains.

### bc

Compatibility fixes were added for modern system headers, including `ptrdiff_t` handling.

### jsonfilter

The bundled Lemon parser code contains old-style function declarations that conflict with modern compiler behavior.

Explicit function prototypes were added so `jsonfilter` can compile with modern GCC.

### squashfs / squashfskit4

Legacy signal-handler declarations were updated to match modern compiler expectations.

### iproute2

The legacy OpenWrt package definitions for `ip-full` and `tc` referenced `libcap`.

Those package dependencies were removed because the legacy OpenWrt 19.07.10 package tree does not provide the required `libcap` package.

The existing optional capability detection in iproute2 remains separate from the OpenWrt package dependency.

## Design goals

This project is **not** intended to turn OpenWrt 19.07.10 into a completely new OpenWrt release.

The goals are:

1. Keep the original OpenWrt 19.07.10 base.
2. Make the legacy build system usable on modern Linux hosts.
3. Keep compatibility changes small and isolated.
4. Avoid unnecessary changes to target-side OpenWrt behavior.
5. Provide a reusable base for other projects.

Project-specific applications and packages should remain outside this repository.

## Building

Clone the repository:

```bash
git clone https://github.com/skye7x/openwrt-19.07.10-modernized.git
cd openwrt-19.07.10-modernized
```

Update feeds if required by your configuration:

```bash
./scripts/feeds update -a
./scripts/feeds install -a
```

Configure the target:

```bash
make menuconfig
```

Build:

```bash
make -j$(nproc)
```

For a verbose build:

```bash
make -j$(nproc) V=s
```

## Linksys WRT160NL

The modernization work has been validated with a complete OpenWrt 19.07.10 build for:

* Target System: `Atheros AR7xxx/AR9xxx`
* Subtarget: `Generic`
* Target Profile: `Linksys WRT160NL`
* Architecture: `mips_24kc`
* Kernel: `4.14.275`

Generated firmware:

```text
bin/targets/ar71xx/generic/
├── openwrt-ar71xx-generic-wrt160nl-squashfs-factory.bin
└── openwrt-ar71xx-generic-wrt160nl-squashfs-sysupgrade.bin
```

The build successfully produced the kernel, target packages, root filesystem and final SquashFS firmware images.

## NREPlatform

This repository is used as the modernized OpenWrt base for **NREPlatform**.

NREPlatform adds its own OpenWrt package containing the NRE daemon and `procd` service integration.

The NREPlatform package is intentionally **not included in this repository**.

NREPlatform:

```text
https://github.com/skye7x/NREPlatform
```

Example project structure:

```text
NREPlatform/
├── src/
├── package/
└── openwrt-19.07.10/
        ↓
    skye7x/openwrt-19.07.10-modernized
```

This allows the same modernized OpenWrt base to be reused by other projects without coupling those projects to NREPlatform.

## Reusing this fork

The repository can be used as a base for other projects that require OpenWrt 19.07.10.

For example:

```bash
git clone https://github.com/skye7x/openwrt-19.07.10-modernized.git
```

Projects can then add their own packages, configuration and device-specific changes without modifying the common modernization layer.

## Scope

This project focuses on maintaining build compatibility for the legacy OpenWrt 19.07.10 release.

It is not intended to replace newer OpenWrt releases.

When possible, newer OpenWrt releases should be preferred. This repository exists for projects that specifically require the OpenWrt 19.07.10 base, legacy targets or compatibility with existing hardware/software.

## Status

The modernization work has been successfully validated with a complete OpenWrt 19.07.10 build for the Linksys WRT160NL.

Verified build stages include:

* host tools
* cross-compiler toolchain
* Linux kernel
* target packages
* root filesystem
* SquashFS filesystem
* factory image
* sysupgrade image

## License

This repository contains OpenWrt 19.07.10 and associated upstream components.

The original licenses of OpenWrt and its individual components remain applicable.

See the individual source files and package metadata for detailed licensing information.
