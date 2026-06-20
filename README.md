
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

Rules are declared as a flat array in your config file, with fields:

| Field      | Type    | Description |
|------------|---------|-------------|
| `function` | symbol  | Widget function to run |
| `y`, `x`   | float   | Top-left corner position (row, col). Fractional if `RELATIVE_*_POS` is set |
| `h`, `w`   | float   | Height and width in rows/cols. Fractional if `RELATIVE_*SIZE` is set |
| `time`     | int     | Refresh interval in seconds. `0` = run once |
| `NULL`     |         | Reserved (leave as `NULL`) |
| `flags`    | bitmask | Display options (see below). |
| `argument` | string  | Widget-specific argument |

### Flags

| Flag               | Description |
|--------------------|-------------|
| `CENTER_Y`         | Center widget vertically |
| `CENTER_X`         | Center widget horizontally |
| `CENTER`           | Center both axes ( shorthand for `CENTER_Y\|CENTER_X`) |
| `RELATIVE_Y_POS`   | Interpret `y` as a fraction of screen height |
| `RELATIVE_X_POS`   | Interpret `x` as a fraction of screen width |
| `RELATIVE_POS`     | Both relative positions |
| `RELATIVE_Y_SIZE`  | Interpret `h` as a fraction of screen height |
| `RELATIVE_X_SIZE`  | Interpret `w` as a fraction of screen width |
| `RELATIVE_SIZE`    | Both relative sizes |
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

### `timedate`
Displays the current date and time using a `strftime` format string. Updates every `time` seconds.

### `image` *(notcurses only)*
Displays an image file, scaled to the widget area. Checks every `time` seconds and re-renders if the file's modification time increases.

## Keys

| Key          | Action |
|--------------|--------|
| `q` / `Q`   | Quit |
| `Space` / `Enter` | Force refresh of all widgets |
