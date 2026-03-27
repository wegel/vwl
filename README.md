# vwl

wlroots-based Wayland compositor with virtual outputs and physical cursor continuity.
Originally forked from dwl.

`LOC: 6418 total, 2578 vwl.c`

## Features

- virtual outputs (split physical monitors into independent workspaces)
- physical cursor continuity (smooth cursor movement across monitor gaps)
- master/stack tiling
- tabbed layout
- fullscreen modes (virtual/monitor)
- per-workspace layout state
- XWayland support

## Keybinds

`mod` = logo key

- `mod+return` spawn terminal
- `mod+d` spawn menu
- `mod+q` kill client
- `mod+j/k` focus next/prev
- `mod+h/l` adjust master width
- `mod+m` zoom (swap master)
- `mod+f` toggle fullscreen (`virtual -> monitor -> off`)
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

- virtual fullscreen: fills virtual output region
- monitor fullscreen: fills entire physical monitor

Toggle with `mod+f` cycles through: `off -> virtual -> monitor -> off`

## Physical Cursor Continuity

Seamless cursor tracking across monitor gaps. Set physical dimensions in `monrules[]`.

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

Optional `xdg-desktop-portal-wlr` chooser override:

`~/.config/xdg-desktop-portal-wlr/vwl`

```ini
[screencast]
chooser_type=dmenu
chooser_cmd=wofi -d --prompt='Select a source to share:' -L 15
```

## Status

Work in progress, used daily by the dev.
