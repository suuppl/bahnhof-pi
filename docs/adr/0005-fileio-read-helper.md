---
status: accepted
---

# Add a read counterpart to fileio.c; keep the brightness curve seam as-is

`brightness_curve.c`/`brightness_ctl.c` is the one genuinely deep, well-seamed split in the codebase (a pure sigmoid function with no I/O, versus a daemon owning all the messy DDC/I2C/UART adapters) — explicitly **not** a candidate for change. The one nearby friction: `brightness_ctl.c`'s `read_sun_times` (29-52, the slurp portion) and `display.c`'s `read_file` (125-137) both hand-roll the same fopen/fseek/ftell/malloc/fread slurp, while `fileio.c` today only owns the write side (`write_atomic`/`copy_atomic`). Decision: add `char *read_file(const char *path)` to `fileio.h`/`fileio.c` (malloc'd, NUL-terminated buffer; caller frees; `NULL` on error), matching the existing `int`/`-1`-on-error convention's spirit for the write side. `display.c`'s local `static read_file` is deleted in favor of it (same name, no collision once the static is gone); `brightness_ctl.c`'s `read_sun_times` calls it for the slurp step and keeps its own JSON-parsing/field-extraction logic.

Lowest-priority item in this review — small, self-contained, no dependency on the other ADRs.
