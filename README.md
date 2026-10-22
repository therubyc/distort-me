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
| `split` | Red, green and blue drift apart along `--angle`. Bright areas split the most, black does nothing. |
| `slice` | The image is cut into horizontal bands `--cell` pixels high. The map's brightness at each band's centre slides it left or right; 50 % grey does nothing. |
| `blocks` | Bright areas break into square pixel blocks, up to `--strength` pixels wide. Black does nothing. |

## Map library

The panel comes with 14 maps to start with: marble, liquid, caustics, ripples, reeded and frosted glass, crumpled paper, woven fabric, wood, brushed and hammered metal, cracked earth, clouds and VHS bands. They are 2048 × 2048, tile seamlessly and are generated from scratch by `tools/maps/make_maps.py` (no photos), so they are released under CC0: use them for anything. Run the script once after cloning; only the thumbnails are checked in.

```sh
python3 tools/maps/make_maps.py      # writes panel/maps/*.png (needs numpy and Pillow)
```

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
src/                  core: map preparation, the six modes, threading
cli/                  command-line test tool (uses stb_image)
plugin/common/        Photoshop filter: protocol, settings, actions, map loading
plugin/win/           Windows dialog and resources
plugin/mac/           macOS dialog (not built yet)
plugin/PiPL.json      plug-in description Photoshop reads (Windows and macOS)
panel/                Photoshop panel (UXP) with live preview, see panel/README.md
tools/maps/           generator of the built-in map library (CC0)
tests/                core tests (ctest), JS port tests and the fake Photoshop host
samples/              test image and maps
third_party/stb/      stb_image and stb_image_write (public domain / MIT)
```

The core knows nothing about Photoshop. The plug-in is a thin wrapper around it.

## Photoshop SDK

The plug-in needs Adobe's Photoshop Plug-In SDK, which you download yourself from the [Adobe Developer Console](https://developer.adobe.com/console/servicesandapis). Its license does not allow redistribution, so it is never part of this repository. This repository contains no Adobe sample code.

## License

MIT, see [LICENSE](LICENSE). Third-party parts are listed in [CREDITS.md](CREDITS.md). The built-in maps in `panel/maps` are CC0 1.0.
