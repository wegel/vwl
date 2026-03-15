# Waybar Howto

This is the simplest way to use vwl with Waybar today:

- use Waybar `custom/*` modules
- use `vwlctl subscribe` as the data source
- turn vwl state into Waybar JSON with `jq`

The IPC already exposes the data a bar needs:

- focused output
- active window title/appid
- physical outputs
- virtual outputs
- workspace to virtual-output mapping

## Requirements

- `vwlctl` in `$PATH`
- `jq`
- Waybar with `custom/*` modules enabled

## State Shape

`vwlctl subscribe` emits:

- one initial reply:

```json
{"id":1,"ok":true,"state":{...}}
```

- then continuous events:

```json
{"type":"event","event":"state","state":{...}}
```

In both cases, the useful compositor state is under `.state`.

## Workspaces

This example shows workspaces on the focused output as a compact text list.

Create `~/.config/waybar/scripts/vwl-workspaces.sh`:

```sh
#!/bin/sh
vwlctl subscribe | jq --unbuffered -c '
  .state as $s
  | $s.focused_output as $out
  | [ $s.workspaces[]
      | select(.output == $out)
      | {
          id,
          focused,
          visible,
          urgent
        }
    ] as $ws
  | {
      text: (
        $ws
        | map(
            if .focused then "[" + (.id|tostring) + "]"
            elif .urgent then "!" + (.id|tostring)
            elif .visible then "(" + (.id|tostring) + ")"
            else (.id|tostring)
            end
          )
        | join(" ")
      ),
      tooltip: (
        $ws
        | map(
            (.id|tostring)
            + if .focused then " focused" else "" end
            + if .visible and (.focused|not) then " visible" else "" end
            + if .urgent then " urgent" else "" end
          )
        | join("\n")
      )
    }
'
```

Make it executable:

```sh
chmod +x ~/.config/waybar/scripts/vwl-workspaces.sh
```

Waybar config:

```json
{
  "custom/vwl-workspaces": {
    "return-type": "json",
    "exec": "~/.config/waybar/scripts/vwl-workspaces.sh",
    "format": "{}",
    "tooltip": false,
    "interval": "persist"
  }
}
```

## Active Window

Create `~/.config/waybar/scripts/vwl-window.sh`:

```sh
#!/bin/sh
vwlctl subscribe | jq --unbuffered -c '
  .state as $s
  | ($s.outputs[] | select(.focused)) as $out
  | {
      text: (
        if $out.active_window == null then ""
        else ($out.active_window.title // "")
        end
      ),
      tooltip: (
        if $out.active_window == null then ""
        else ($out.active_window.appid // "")
        end
      )
    }
'
```

Make it executable:

```sh
chmod +x ~/.config/waybar/scripts/vwl-window.sh
```

Waybar config:

```json
{
  "custom/vwl-window": {
    "return-type": "json",
    "exec": "~/.config/waybar/scripts/vwl-window.sh",
    "format": "{}",
    "tooltip": false,
    "interval": "persist",
    "max-length": 80
  }
}
```

## Focused Output / Virtual Output

If you want a small monitor/vout indicator:

Create `~/.config/waybar/scripts/vwl-output.sh`:

```sh
#!/bin/sh
vwlctl subscribe | jq --unbuffered -c '
  .state as $s
  | ($s.outputs[] | select(.focused)) as $out
  | ($s.virtual_outputs[] | select(.focused)) as $vout
  | {
      text: ($out.name + ":" + $vout.name),
      tooltip: (
        "output: " + $out.name + "\n"
        + "vout: " + $vout.name + "\n"
        + "workspace: " + ($vout.workspace_name // "")
      )
    }
'
```

## Example Waybar Snippet

```json
{
  "modules-left": ["custom/vwl-workspaces"],
  "modules-center": ["custom/vwl-window"],
  "modules-right": ["custom/vwl-output"],

  "custom/vwl-workspaces": {
    "return-type": "json",
    "exec": "~/.config/waybar/scripts/vwl-workspaces.sh",
    "format": "{}",
    "tooltip": false,
    "interval": "persist"
  },
  "custom/vwl-window": {
    "return-type": "json",
    "exec": "~/.config/waybar/scripts/vwl-window.sh",
    "format": "{}",
    "tooltip": false,
    "interval": "persist",
    "max-length": 80
  },
  "custom/vwl-output": {
    "return-type": "json",
    "exec": "~/.config/waybar/scripts/vwl-output.sh",
    "format": "{}",
    "tooltip": false,
    "interval": "persist"
  }
}
```

## CSS

Waybar custom modules do not use the same CSS selectors as built-in modules.

Use:

- `#custom-vwl-workspaces`
- `#custom-vwl-window`
- `#custom-vwl-output`

For example:

```css
#custom-vwl-window {
    background-color: transparent;
    color: #ddc7a1;
    border: none;
    margin: 6px 12px;
    padding: 0;
}
```
