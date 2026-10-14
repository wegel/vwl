# vwl

wlroots-based Wayland compositor with virtual outputs and physical cursor continuity.
Originally forked from dwl.

`LOC: 8126 total, 1270 vwl.c`

## Features

- virtual outputs (split physical monitors into independent workspaces)
- physical cursor continuity (smooth cursor movement across monitor gaps)
- master/stack tiling
- tabbed layout
- fullscreen modes (virtual/monitor)
- per-workspace layout state
- child dialogs centered over their parent window
- XWayland support

## Keybinds

`mod` = logo key

- `mod+return` spawn terminal
- `mod+d` spawn menu
- `mod+q` kill client
- `mod+j/k` focus next/prev
- `mod+h/l` adjust master width
- `mod+m` zoom (swap master)
- `mod+f` cycle fullscreen (skips virtual fullscreen when it duplicates monitor fullscreen)
- `mod+t` toggle tabbed layout
- `mod+space` cycle layout
- `mod+shift+e` quit compositor
- `mod+comma` focus virtual output left
- `mod+period` focus virtual output right
- `mod+shift+</>` move client to monitor left/right
- `mod+0-9` view workspace `0-9`
- `mod+shift+0-9` move client to workspace `0-9`
- `mod+ctrl+shift+h/j/k/l` move workspace to virtual output

## Building

```sh
make
```

Run `make check` to test floating sizes, child dialogs, fullscreen, focus/tab order, workspace moves, display reconnects,
keep-awake requests, and captured cursor pixels with separate headless
compositors and Wayland test apps. The tests use
temporary sockets and do not change your running desktop. They also require
the `wayland-client` development files. The capture test uses software rendering
by default; `VWL_TEST_RENDERER=gles2 ./tests/capture-test` checks the GPU renderer
when a render device is available.

## Source layout

`vwl.c` owns window lifetimes, focus, floating/fullscreen behavior, and startup.
The desktop features live in these files:

| File | Owns |
| --- | --- |
| `workspace.c` | Workspaces, virtual outputs, and workspace moves |
| `output.c` | Monitor discovery, removal, screen geometry, and monitor focus |
| `cursor.c` | Pointer motion, window dragging, and physical screen crossings |
| `layout.c` | Tiled/tabbed layouts and tab order |
| `debug.c` | The human-readable state dump |
| `plumbing.c` | Wayland callbacks, input devices, layers, and session locks |

`vwl.h` declares the shared types and functions, grouped by source file.
The desktop modules read small views of `config.h`; the user configuration format
is unchanged. `tests/compositor.h` supplies the private compositor and client
process helpers used by the integration tests.

## Running

```sh
./vwl
```

## IPC

Shell-facing IPC over a Unix socket.

- socket: `$XDG_RUNTIME_DIR/vwl.sock`
- client: `vwlctl`
- docs: `docs/ipc.md`, `docs/waybar-howto.md`

```sh
vwlctl get-state
vwlctl subscribe
vwlctl set-workspace 3
```

## Dependencies

- `wlroots 0.20`
- `wayland-server`
- `xkbcommon`
- `libinput`
- `pixman`
- optional: `xcb`, `xcb-icccm` (for XWayland)

## Configuration

Edit `config.def.h` and recompile.

Key settings:

- physical cursor gap jumps: `enable_physical_cursor_gap_jumps`
- virtual output rules: `vorules[]`
- monitor rules: `monrules[]`
- keyboard/trackpad settings

## Virtual Outputs

Split physical monitors into named regions. Each region gets its own workspace.
Move workspaces between regions with `mod+ctrl+shift+hjkl`.

The compositor keeps those moves when a display reconnects. It restores each
workspace to its last display and region, with the previously visible workspace
active again. Workspace lists in `vorules[]` set startup defaults for workspaces
that have no current or saved home.

```c
static const VirtualOutputRule vorules[] = {
    /* monitor  name       x    y     w    h   mfact nmaster lt[0]        lt[1] */
    { "DP-1",   "left",    0,   0,  960, 1080, 0.55f, 1, &layouts[0], &layouts[1] },
    { "DP-1",   "right", 960,   0,  960, 1080, 0.55f, 1, &layouts[0], &layouts[1] },
};
```

- `w/h` of `0` = expand to monitor's remaining space
- `mfact` = master area factor (`0.0-1.0`)
- `nmaster` = number of master windows
- `lt[0]/lt[1]` = primary/secondary layout functions

## Fullscreen Modes

- virtual fullscreen: fills the virtual output's usable area, keeping panels and tab headers visible
- monitor fullscreen: fills the entire physical monitor, covering ordinary panels and tab headers

`mod+f` normally cycles through `off -> virtual -> monitor -> off`. When the
virtual output fills the physical monitor with no reserved panel space, it uses
`off -> monitor -> off`. Tabbed workspaces keep the virtual step so you can
still switch tabs, even if the tab header is hidden.

The compositor checks the current workspace and display on each keypress.
Moving a workspace or changing its layout resizes a fullscreen window within
its chosen scope; the next keypress skips any virtual-to-monitor step that
would now repeat the same area and keep no tab controls.

An app's fullscreen request enters the same first mode as `mod+f`. Repeated
requests keep its current mode. A normal window that already fills its
workspace still enters fullscreen, removing its border and letting the app
hide its own controls. Floating windows regain their original size when
fullscreen ends. Opening another ordinary window in the same workspace exits
monitor fullscreen; virtual fullscreen stays active.

## Floating Window Sizes

The first time a window switches from tiled to floating, vwl uses the app's
initial content size, capped by the configured floating width/height factors
and the current virtual output's usable area.

When a floating window returns to tiling, vwl remembers its content size. The
next switch to floating restores that size, shrinking it only if needed to fit
the current usable area, including space reserved for tab headers. The size
factors do not override this saved size. `center_floating_windows` still controls
whether vwl centers the window when it floats.

Each window keeps its saved size in memory until it closes. Fullscreen uses a
separate saved rectangle; changing fullscreen modes does not replace this size.

## Child Dialogs

vwl opens floating child dialogs centered over their parent, keeping them inside
the parent’s virtual output when they fit. Larger dialogs keep their requested
size and start at the output’s top-left corner. XWayland dialogs keep their
requested screen position unless it is `(0, 0)`, which uses the same centering
rule. vwl places dialogs when they open; later redraws do not recenter them.

## Physical Cursor Continuity

Set physical dimensions in `monrules[]` to keep the pointer at the same real-world
height or horizontal position when it crosses screens. vwl also uses these
coordinates for the cursor image on each screen, so a fragment visible across an
edge appears in the right place before the pointer tip crosses.

Without explicit origins, vwl places each screen beside a neighbour using their
physical sizes. Explicit origins anchor nearby automatic screens. vwl keeps
automatic screens from overlapping, but desktop pixels cannot describe every
mixed-density layout exactly. Set `origin_is_set` and `x_mm`/`y_mm` on each screen
when you need to match its exact position on your desk. Explicit origins are
never moved, even if they overlap. Rotated screens need physical dimensions in
the orientation in which you use them.


```c
static const MonitorRule monrules[] = {
    /* name      scale transform          x    y   phys{} */
    { "DP-1",    1.0f, WL_OUTPUT_TRANSFORM_NORMAL, 0, 0, {
        .width_mm = 520,    /* physical width in mm */
        .height_mm = 320,   /* physical height in mm */
        .x_mm = 0,          /* physical X position */
        .y_mm = 0,          /* physical Y position */
        .size_is_set = 1,   /* use explicit size */
        .origin_is_set = 1, /* use explicit origin */
    }},
    { NULL, 1, WL_OUTPUT_TRANSFORM_NORMAL, -1, -1, {} }, /* fallback */
};
```

- `x/y` = pixel position (`-1,-1 = auto`)
- `scale` = HiDPI factor
- `transform` = rotation (`NORMAL/90/180/270/FLIPPED_*`)
- `phys{}` = real-world dimensions for cursor math

## Howtos

### Startup Script

`~/.config/vwl/run`

```sh
#!/bin/sh
waybar &
swayidle -w \
  timeout 300 'swaylock -f' \
  timeout 600 'wlopm --off \*' \
  resume 'wlopm --on \*' \
  before-sleep 'swaylock -f' &
```

### Manual Lock With `swayidle`

`config.h`

```c
static const char *lockcmd[] = { "sh", "-c", "sleep 1 && killall -USR1 swayidle", NULL };
/* ... */
{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_L, spawn, {.v = lockcmd} },
```

`SIGUSR1` triggers `swayidle`'s lock timeout, ensuring wake-on-input works.

### Screenshot

`config.h`

```c
static const char *screenshotcmd[] = { "sh", "-c",
  "grim -g \"$(slurp)\" ~/Pictures/Screenshots/$(date +'%Y-%m-%d_%H-%M-%S').png", NULL };
static const char *screenshotclipboardcmd[] = { "sh", "-c",
  "slurp | grim -g - - | wl-copy", NULL };
/* ... */
{ 0,     XKB_KEY_Print, spawn, {.v = screenshotcmd} },
{ MODKEY, XKB_KEY_Print, spawn, {.v = screenshotclipboardcmd} },
```

### Window Sharing

`~/.config/vwl/run`

```sh
export XDG_CURRENT_DESKTOP=vwl:wlroots
dbus-update-activation-environment --systemd WAYLAND_DISPLAY XDG_CURRENT_DESKTOP
systemctl --user restart xdg-desktop-portal.service xdg-desktop-portal-wlr.service >/dev/null 2>&1 &
```

This lets the portal services bind to the current `vwl` session.

Window and virtual-output shares include the actual pointer when the capture
client requests it, including client-drawn and animated cursors. The pointer is
clipped at the shared area's edges. Capture clients can also request frames
without a pointer, or receive its image and position separately.

Virtual-output sharing additionally requires a patched `xdg-desktop-portal-wlr`:
`https://github.com/wegel/xdg-desktop-portal-wlr/tree/vwl-support`

Optional `xdg-desktop-portal-wlr` chooser override:

`~/.config/xdg-desktop-portal-wlr/vwl`

```ini
[screencast]
chooser_type=dmenu
chooser_cmd=wofi -d --prompt='Select a source to share:' -L 15
```

## Status

Work in progress, used daily by the dev.
