# Moonlight Mac

## Disclaimer
This fork was created solely to learn more about PC streaming and the MacOS operating system to optimize it for my setup. Due to this, it has a bunch of experiental/in-progress features added that may or may not work for you. I recommend using the [official Moonlight client](https://github.com/moonlight-stream/moonlight-qt) for Mac as it's still being updated and will give you the most stable experience.

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
 - AV1 hardware decoding support (Apple Silicon M3 and above)
 - Clipboard Sync (Requires [Apollo](https://github.com/ClassicOldSong/Apollo) or [Vibepollo](https://github.com/Nonary/Vibepollo))
 
## Downloads
- [macOS](https://github.com/moyogii/moonlight-mac/releases)

## Building

### macOS Build Requirements
* Qt 6.9.2 SDK or later (Qt 6.9.2+ required for macOS Tahoe compatibility)
* Xcode 14 or later (earlier versions may work but are not officially supported)
* [create-dmg](https://github.com/sindresorhus/create-dmg) (only if building DMGs for use on non-development Macs)

### Build Setup Steps
1. Install the latest Qt SDK (and optionally, the Qt Creator IDE) from https://www.qt.io/download
    * You can install Qt via Homebrew on macOS: `brew install qt@6`
    * You may also use your Linux distro's package manager for the Qt SDK as long as the packages are Qt 5.9 or later.
    * This step is not required for building on Steam Link, because the Steam Link SDK includes Qt 5.14.
2. Run `git submodule update --init --recursive` from within `moonlight-qt/`
    * This pulls in all dependencies including FFmpeg, SDL2, and OpenSSL from the `libs` submodule
3. Open the project in Qt Creator or build from qmake on the command line.
    * To build a binary for use on non-development machines, use the scripts in the `scripts` folder.
        * For macOS builds, the GitHub Actions workflow automatically creates DMG files. For manual DMG creation, use `create-dmg` directly.
    * To build from the command line for development use on macOS or Linux:
        ```bash
        qmake6 moonlight-qt.pro
        make debug  # or 'make release'
        ```
    * **Validation Script**: Use `./scripts/validate-build.sh` to verify your build environment is correctly configured
    * **GitHub Actions**: The project includes automated macOS builds that create distributable DMG files:
        * Builds are triggered on pushes to main/master branches and pull requests
        * Release builds are automatically attached to GitHub releases
        * DMG artifacts are available for download from the Actions tab
    * To create an embedded build for a single-purpose device, use `qmake6 "CONFIG+=embedded" moonlight-qt.pro` and build normally.
        * This build will lack windowed mode, Discord/Help links, and other features that don't make sense on an embedded device.
        * For platforms with poor GPU performance, add `"CONFIG+=gpuslow"` to prefer direct KMSDRM rendering over GL/Vulkan renderers. Direct KMSDRM rendering can use dedicated YUV/RGB conversion and scaling hardware rather than slower GPU shaders for these operations.

## Contribute
1. Fork it
2. Write code
3. Send Pull Requests

## Credits
- [Moonlight](https://github.com/moonlight-stream/moonlight-qt)
- [Apollo](https://github.com/ClassicOldSong/Apollo)

Make sure to check out the [Official Moonlight Website](https://moonlight-stream.org) and support them.
