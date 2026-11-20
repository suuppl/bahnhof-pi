# Third-party components

The project's own code is released into the public domain ([`LICENSE`](LICENSE),
The Unlicense). It ships or uses these third-party components under their own
licenses:

| Component | Where | License | Notice / full text |
|-----------|-------|---------|--------------------|
| LVGL 9.2.2 | `lvgl/` (git submodule) | MIT | `lvgl/LICENCE.txt` |
| Noto Emoji 3.002 (Google) | `fonts/NotoEmoji-Regular.ttf` | SIL Open Font License 1.1 | [`fonts/OFL-NotoEmoji.txt`](fonts/OFL-NotoEmoji.txt) |
| Twemoji Mozilla 0.7.0 | `fonts/Twemoji.Mozilla.ttf` | Graphics CC-BY 4.0 (Twitter, Inc and other contributors); font build Apache-2.0 (Mozilla) | [`fonts/LICENSE-Twemoji.txt`](fonts/LICENSE-Twemoji.txt) |
| DejaVu Sans | not bundled — loaded from the Pi's `/usr/share/fonts/truetype/dejavu/` | Bitstream Vera / DejaVu license | Debian package `fonts-dejavu-core` |

The cross build links against libcurl, libxml2, cJSON and FreeType from the
Pi (`make sysroot` copies them from your own Pi; nothing is bundled here).

Data sources (not bundled): departure data from Deutsche Bahn's IRIS
timetable API; weather data by [Open-Meteo.com](https://open-meteo.com/)
(CC-BY 4.0).

## Attributions

- Emoji graphics: [Twemoji](https://github.com/twitter/twemoji) by Twitter,
  Inc and other contributors, licensed under
  [CC-BY 4.0](https://creativecommons.org/licenses/by/4.0/).
- Portions of this software are copyright © 2024 The FreeType Project
  (www.freetype.org). All rights reserved.
- Weather data by [Open-Meteo.com](https://open-meteo.com/).
