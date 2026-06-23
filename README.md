
# show

Simple, suckless-inspired, configurable terminal dashboard.

Display the output of multiple commands and widgets simultaneously in a multi-threaded [notcurses](https://github.com/dankamongmen/notcurses) or ncurses session, each refreshing at its own interval.

## Requirements

- ncurses or [notcurses](https://github.com/dankamongmen/notcurses)
- pthreads
- libvterm
- libutil (for PTY support in `external_command`)


## Installation & Configuration

Copy and modify `example.config.c` under a new name, then run `make`.
To use ncurses instead of notcurses, remove the `#define USE_NOTCURSES` line from your config.

Widgets are declared as an array in your config file using designated initializers.

```c
static struct widget widgets[] = {
    {
        .widget = large_clock, .data = "%H:%M:%S", .time = 1,
        .h = {SZ_ABS, 5}, .w = {SZ_ABS, 40},
        .y = {.ref_point = START, .offset = 1},            //1 row from the top
        .x = {.self_point = CENTER, .ref_point = CENTER},  //centered horizontally
    },
    {
        .widget = timedate, .data = "%A %d %B", .time = 60,
        .h = {SZ_ABS, 1}, .w = {SZ_MATCH, 0, WIDGET(0)},  //as wide as the clock (index 0)
        .x = {.ref = WIDGET(0), .ref_point = START, .self_point = START},
        .y = {.ref = WIDGET(0), .ref_point = END, .offset = 1}, //1 row below the clock
    },
};
```

A widget may reference only widgets that appear **earlier** in the array: `i` must be less than the referencing widget's own index. References are resolved top to bottom in a single pass, so a forward or self reference reads geometry that hasn't been computed yet.

Fields of `struct widget`:

| Field      | Type           | Description |
|------------|----------------|-------------|
| `widget`   | symbol         | Widget thread function to run |
| `y`, `x`   | `struct anchor`| Placement of the top edge (`y`) and left edge (`x`) — see below |
| `h`, `w`   | `struct extent`| Height and width — see below |
| `time`     | int            | Refresh interval in seconds. `0` = run once |
| `window`   |                | Backend handle, filled at layout (leave unset) |
| `flags`    | bitmask        | Rendering options (see below) |
| `data`     | string         | Widget-specific argument |

### Placement (`y` / `x` anchors)

Each axis positions the widget by aligning one **point of the widget** to one **point of a reference**, plus an offset:

```
this.<self_point>  =  reference.<ref_point>  +  offset
```

`struct anchor` fields (optional, an omitted anchor puts that edge at the screen origin):

| Field        | Values                  | Meaning |
|--------------|-------------------------|---------|
| `ref`        | `SCREEN` *(default)* / `WIDGET(i)` | Reference: the screen, or the widget at index `i` |
| `ref_point`  | `START` / `CENTER` / `END` | Which point of the reference (top/mid/bottom on `y`, left/mid/right on `x`) |
| `self_point` | `START` / `CENTER` / `END` | Which point of this widget aligns to it |
| `offset`     | number                  | Gap added after alignment, in cells |
| `rel`        | `0` / `1`               | If set, `offset` is a fraction of the screen's size along this axis |

Common patterns: center on screen → `self_point = CENTER, ref_point = CENTER`; hug the right edge → `self_point = END, ref_point = END`; directly below 4th widget with a 1-row gap → `{.ref = WIDGET(3), .ref_point = END, .offset = 1}`; 20% down from the top → `{.offset = 0.2, .rel = 1}`.

### Size (`h` / `w` extents)

`struct extent` fields:

| Field   | Values                         | Meaning |
|---------|--------------------------------|---------|
| `mode`  | `SZ_ABS` *(default)* / `SZ_REL` / `SZ_MATCH` | Absolute cells / fraction of screen / equal to a reference's size |
| `value` | number                         | Cell count (`SZ_ABS`) or fraction 0–1 (`SZ_REL`); ignored for `SZ_MATCH` |
| `ref`   | `WIDGET(i)`                      | Widget whose size to match, for `SZ_MATCH` |

### Flags

Rendering options only:

| Flag               | Description |
|--------------------|-------------|
| `DRAW_BOX`         | Draw a border around the widget |
| `BOLD`             | Bold text |
| `ITALIC`           | Italic text |
| `OPAQUE`           | Opaque background *(notcurses only)* |
| `BLEND_BACKGROUND` | Semi-transparent background *(notcurses only)* |

### Global settings

```c
#define PROGRAM_LOCALE ""          // locale passed to setlocale; "" uses system default
#define REFRESH_MICROSECONDS 200000 // screen redraw interval
```

## Widgets

### `external_command`
Runs a shell command in a pseudo-terminal and renders its output, including full ANSI color and text attributes (bold, italic, underline, reverse, blink, etc). The command runs to completion and is rerun every `time` seconds.

### `live_external_command`
Like `external_command`, but launches the command **once** and streams its live output as it arrives, for long-running or interactive commands (`top`, `tail -f`, a TUI). `time` is ignored. The command is restarted when the display is rebuilt (resize/refresh). If it exits on its own, the last frame stays on screen.

### `text_external_command`
Runs a shell command and captures plain text output line by line. Lighter than `external_command`, no PTY or color support.

### `print_string`
Renders a static string.

### `print_large_string`
Renders a static string in large block letters scaled to fill the widget area. Supports uppercase ASCII (lowercase is folded to uppercase; unsupported characters render as blank space). The glyphs live in `big_font.h`.

### `timedate`
Displays the current date and time using a `strftime` format string. Updates every `time` seconds.

### `large_clock`
Displays the time in large block digits scaled to fill the widget area. Takes a `strftime` format string in the argument that should resolve to only digits and `:` (e.g. `%H:%M` or `%H:%M:%S`). Updates every `time` seconds.

### `image` *(notcurses only)*
Displays an image file, scaled to the widget area. Checks every `time` seconds and re-renders if the file's modification time increases.

## Plot widgets

Plot widgets sample a shell command over time and render the value graphically. Instead of a string, their `data` field points to a `plot_data` struct:

```c
struct plot_data{
    const char* source; // shell command that prints a number or, for the `_file` widgets, a file path
    double min, max;    // expected value range
    uint32_t color;     // 0xRRGGBB main color; 0 = terminal default foreground
    uint32_t bg_color;  // 0xRRGGBB color of the unfilled parts; 0 = terminal default background
    int flags;          // flags
};
```

Each plot comes in three flavours distinguished by how `source` is read: the plain widget reruns `source` as a command every `time` seconds; the `_live` widget launches `source` once and streams values from its output; the `_file` widget treats `source` as a **file path**, polls its modification time every `time` seconds, and redraws when it changes.

`plot_data.flags` can request axes, drawn inside the widget's own area (so enabling one **shrinks the plot**, it doesn't enlarge the widget):

| Flag           | Effect | Space taken |
|----------------|--------|-------------|
| `PLOT_Y_AXIS`  | Vertical axis line down the left edge | 1 column |
| `LABEL_Y_AXIS` | Value labels left of the Y line (max on the top row, min on the bottom) | +6 columns (7 total with the line) |
| `PLOT_X_AXIS`  | Horizontal baseline along the bottom | 1 row |
| `LABEL_X_AXIS` | Labels under the baseline | +1 row (2 total with the line) |

A `LABEL_` flag only takes effect when its matching `PLOT_` flag is also set. The number labels are formatted to fit a fixed 6-column field, dropping decimals and falling back to scientific notation as needed. If a margin wouldn't leave at least one plot cell, that axis is dropped entirely.

To declare a widget, supply it with a compound literal (or a named static):

```c
{ .widget = progressbar, .time = 1,
  .data = &(struct plot_data){ .source = "cat /sys/class/power_supply/BAT0/capacity",
                               .min = 0, .max = 100, .color = 0x00ff00 },
  .h = {SZ_ABS, 1}, .w = {SZ_ABS, 40} },
```

Every widget below also comes in `_live` and `_file` forms (e.g. `progressbar_live`, `bar_sparkline_file`) as described under flavours above.

| Widget | Renders | Notes |
|--------|---------|-------|
| `progressbar` | Horizontal bar filling left→right between `min` and `max`, accurate to 1/8 cell | |
| `vertical_progressbar` | Like `progressbar`, but fills bottom→top | |
| `bar_sparkline` | Scrolling history, one vertical eighth-block bar per sample | history length = width |
| `stairs_sparkline` | Same history drawn as a connected box-drawing line | whole-cell resolution. `NaN` breaks the line |
| `braille_sparkline` | Same history as a connected braille-dot line | sub-cell vertical resolution, 2 samples per column (2× history). Requires a braille-capable font. `NaN` breaks the line |

The **sparklines** scroll in from the right (newest sample on the right) and **auto-scale** the vertical range to the on-screen samples when `min == max`; otherwise, like the progress bars, they use the fixed `min`..`max` range.

## Keys

| Key          | Action |
|--------------|--------|
| `q` / `Q`   | Quit |
| `Space` / `Enter` | Force refresh of all widgets |
