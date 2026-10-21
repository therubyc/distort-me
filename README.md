# Distort Me

Free, image-driven distortion for Photoshop on Windows and macOS.

Every effect in Distort Me is steered by an image, a "map". A scan of crumpled paper, a marble texture or a photo of water decides where each pixel of your artwork moves.

> **Status:** early development (0.1). The Windows filter plug-in builds and passes automated tests in a simulated host; first tests in real Photoshop 2026 are under way. The macOS version is prepared but not built yet.

| Source | Displace | Refract | Flow |
| --- | --- | --- | --- |
| ![source](samples/source.png) | ![displace](docs/images/out_displace.png) | ![refract](docs/images/out_refract.png) | ![flow](docs/images/out_flow.png) |

## Modes

| Mode | What the map controls |
| --- | --- |
| `displace` | Red shifts pixels left/right, green shifts them up/down. 50 % grey means no shift, like Photoshop's Displace filter. With `--luminance`, brightness pushes along `--angle`. |
| `refract` | The map is a sheet of glass whose thickness follows its brightness. Light bends through it (Snell's law, `--ior`). |
| `flow` | Pixels travel along the map's lines of equal brightness (or uphill with `--uphill`), which smears the image like ink in water. |

## Install (Windows)

1. Copy `DistortMe.8bf` to `C:\Program Files\Adobe\Adobe Photoshop 2026\Plug-ins\`.
2. Restart Photoshop.
3. Choose **Filter > Distort Me > Distort Me...**, pick a map image and a mode.

Tip: convert the layer to a Smart Object first (**Filter > Convert for Smart Filters**). Then you can double-click the filter later and change any setting.

## Build the plug-in

The plug-in needs Adobe's Photoshop SDK, which you download yourself (see below).

| System | Command | Needs |
| --- | --- | --- |
| Windows | `build-windows.bat C:\path\to\photoshop-sdk` | Visual Studio 2022, CMake |
| macOS | `./build-mac.sh /path/to/photoshop-sdk` | Xcode command line tools, CMake |
| Linux (cross) | `./build-windows-cross.sh /path/to/photoshop-sdk` | MinGW-w64; Wine to run `tests/pshost` |

`tests/pshost` is a small fake Photoshop: it loads the real `.8bf`, runs it through the filter protocol (including action replay) and saves the result, so the plug-in can be tested without Photoshop:

```sh
wine build-win/pshost.exe build-win/DistortMe.8bf samples/source.png samples/map_noise.png out.png 0 25
```

## Build the core and test tool

You need CMake 3.16+ and a C compiler: Visual Studio 2022 on Windows, Xcode command-line tools on macOS, gcc or clang on Linux.

```sh
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release
```

The test tool ends up in `build/` (`build/Release/` with Visual Studio).

## Try it

```sh
distortme samples/source.png samples/map_noise.png out.png --mode displace --strength 25
distortme samples/source.png samples/map_ripple.png out.png --mode refract --strength 60
distortme samples/source.png samples/map_marble.png out.png --mode flow --strength 40
```

Run `distortme` without arguments to list all options.

## Layout

```
include/distortme.h   public API of the core (plain C99)
src/                  core: map preparation, the three modes, threading
cli/                  command-line test tool (uses stb_image)
plugin/common/        Photoshop filter: protocol, settings, actions, map loading
plugin/win/           Windows dialog and resources
plugin/mac/           macOS dialog (not built yet)
plugin/PiPL.json      plug-in description Photoshop reads (Windows and macOS)
tests/                core tests (ctest) and the fake Photoshop host
samples/              test image and maps
third_party/stb/      stb_image and stb_image_write (public domain / MIT)
```

The core knows nothing about Photoshop. The plug-in is a thin wrapper around it.

## Photoshop SDK

The plug-in needs Adobe's Photoshop Plug-In SDK, which you download yourself from the [Adobe Developer Console](https://developer.adobe.com/console/servicesandapis). Its license does not allow redistribution, so it is never part of this repository. This repository contains no Adobe sample code.

## License

MIT, see [LICENSE](LICENSE). Bundled maps will list their source in `CREDITS.md`.
