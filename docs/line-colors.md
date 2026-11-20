# Train line colours — `line_colors.json`

The pill around each train label (`S1`, `RE8`, `ICE1234`) is coloured from
an optional JSON file on the Pi, `APP_DIR/line_colors.json`
(`~/bahnhofstafel/line_colors.json`). The display re-reads it whenever it
changes, so no restart is needed.

```json
{
  "S1": "#aaaaaa",
  "S2": "#bbbbbb",
  "S8": { "bg": "#000000", "fg": "#f0ac32" },
  "RE": "#ec0016"
}
```

- Value: `"#rrggbb"` background, or `{ "bg": "#rrggbb", "fg": "#rrggbb" }` to
  set the text colour too. Without `fg` the text is black or white,
  whichever contrasts better.
- Lookup per train, first hit wins:
  1. exact label: `"S1"`, `"RE8"`
  2. train type, i.e. the label's leading letters: `"S"`, `"RE"`, `"ICE"`
  3. built-in type default: S green, U blue, RE/RB/IRE/MEX red,
     ICE/IC/EC/ECE/RJ/NJ white
  4. grey
- Exact matching means `"S2"` never colours `S20`.

## Deploying

Put `line_colors.json` next to the `Makefile` (git-ignored here; see
`line_colors.example.json`). `make deploy` copies it to the Pi, and
`make colors` copies only that file.
