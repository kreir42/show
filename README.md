
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
    const char* source; // shell command, ran every `time` seconds, that prints a number
    double min, max;    // expected value range
    uint32_t color;     // 0xRRGGBB main color; 0 = terminal default foreground
    uint32_t bg_color;  // 0xRRGGBB color of the unfilled parts; 0 = terminal default background
};
```

Supply it with a compound literal (or a named static):

```c
{ .widget = progressbar, .time = 1,
  .data = &(struct plot_data){ .source = "cat /sys/class/power_supply/BAT0/capacity",
                               .min = 0, .max = 100, .color = 0x00ff00 },
  .h = {SZ_ABS, 1}, .w = {SZ_ABS, 40} },
```

### `progressbar`
A horizontal bar that fills left-to-right in proportion to where the sampled value sits between `min` and `max`. Uses eighth-block glyphs so the bar end is accurate to ⅛ of a cell. Resamples every `time` seconds (or draws once if `time <= 0`).

### `vertical_progressbar`
Like `progressbar`, but fills bottom-to-top.

## Keys

| Key          | Action |
|--------------|--------|
| `q` / `Q`   | Quit |
| `Space` / `Enter` | Force refresh of all widgets |
