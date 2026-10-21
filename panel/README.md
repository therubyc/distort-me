# Distort Me panel (Photoshop 2024 and newer)

A floating panel with a live preview. Load a layer, pick a map, move the
sliders, and press **APPLY** when it looks right. Apply runs the Distort Me
filter at full resolution, by default as a Smart Filter.

The panel needs the filter plug-in (`DistortMe.8bf` on Windows,
`DistortMe.plugin` on macOS) installed as well.

## Install

- **Package:** double-click `DistortMe.ccx`. Creative Cloud installs it and
  warns that it does not come from the Adobe marketplace.
- **Development:** in the UXP Developer Tool choose *Add Plugin*, pick
  `panel/manifest.json` and press *Load*.

Then open **Plugins > Distort Me > Distort Me**.

## Files

```
manifest.json       UXP manifest (panel entry point)
index.html          layout
styles.css          look (Make Me Analog family style)
main.js             Photoshop side: reading layers and maps, applying the filter
js/distortme.js     JavaScript port of the core, used for the preview
icons/title.svg     logo: Big Shoulders Display Black, ME in shade blocks (tools/logo)
```

The preview and the filter use the same algorithms, so the preview matches
the result (`tests/js/test_engine.js` checks the port).
