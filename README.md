# Moonlight

## Disclaimer
Maclight-qt is a fork of the [official Moonlight client](https://github.com/moonlight-stream/moonlight-qt) solely to learn more about PC streaming and the MacOS operating system to optimize it for my specific needs. Due to this, it has a bunch of experimental/in-progress features that may or may not work for you. I *really* recommend using the official Mac client for a stable experience to support the original developers of Moonlight and their hard work as without them this fork would not be possible. 

Occassionally this fork will pull in relevant changes from [moonlight-common-c](https://github.com/moonlight-stream/moonlight-common-c) and [moonlight-qt](https://github.com/moonlight-stream/moonlight-qt) to keep it as up to date as possible with the original app.

## Features
 - Hardware accelerated video decoding on Mac
 - H.264, HEVC, and AV1 codec support (AV1 requires Sunshine and a supported host GPU)
 - YUV 4:4:4 support (Sunshine only)
 - HDR streaming support
 - 7.1 surround sound audio support
 - 10-point multitouch support (Sunshine only)
 - Gamepad support with force feedback and motion controls for up to 16 players
 - Support for both pointer capture (for games) and direct mouse control (for remote desktop)
 - Support for passing system-wide keyboard shortcuts like Alt+Tab to the host
 - Automatic enabling and disabling of AWDL (Apple Wireless Direct Link)
 - Game Mode support
 - Clipboard Sync (Requires [Apollo](https://github.com/ClassicOldSong/Apollo) or [Vibepollo](https://github.com/Nonary/Vibepollo))
 - Programmable Hotkeys (Toggle Performance Overlay, Exit Stream)
 
## Downloads
- [macOS](https://github.com/moyogii/moonlight-mac/releases)

## Building on macOS

### Requirements
* macOS 15.0 (Tahoe) or later
* Xcode 14 or later (install from the App Store, or use `xcode-select --install` for command line tools)
* Qt 6.9.2 SDK or later
* [create-dmg](https://github.com/sindresorhus/create-dmg) (only needed for building distributable DMGs)

### Step 1: Install Dependencies

Install Homebrew if you don't have it (see https://brew.sh), then install the required packages:

```bash
brew install qt@6 pkg-config node
```

For DMG packaging (optional, only needed for distribution):
```bash
npm install --global create-dmg
```

### Step 2: Clone and Initialize Submodules

```bash
git clone --recursive https://github.com/moyogii/moonlight-mac.git
cd moonlight-mac
```

If you already cloned without `--recursive`, pull in the submodules:
```bash
git submodule update --init --recursive
```

This pulls in all dependencies including FFmpeg, SDL2, OpenSSL, and opus from the `libs` submodule.

### Step 3: Verify Build Environment

```bash
./scripts/validate-build.sh
```

This checks that Qt, pkg-config, and submodules are correctly configured.

### Step 4: Build

**Development build (from the command line):**
```bash
qmake6 moonlight-qt.pro
make debug
```

**Release build:**
```bash
qmake6 moonlight-qt.pro
make release
```

**Build a distributable DMG:**
```bash
bash scripts/generate-dmg.sh Release   # or Debug
# Output: build/installer-Release/Moonlight-<version>.dmg
```

You can also open `moonlight-qt.pro` in Qt Creator instead of building from the command line.

## Contribute
1. Fork it
2. Write code
3. Send Pull Requests

## Credits
- [Moonlight](https://github.com/moonlight-stream/moonlight-qt)
- [Apollo](https://github.com/ClassicOldSong/Apollo)

Make sure to check out the [Official Moonlight Website](https://moonlight-stream.org) and support them.
