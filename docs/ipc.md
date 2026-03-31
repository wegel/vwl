# IPC

vwl exposes a shell-facing IPC over a Unix domain socket:

- path: `$XDG_RUNTIME_DIR/vwl.sock`
- transport: newline-delimited JSON
- environment: `VWL_SOCKET` is exported by the compositor for child processes

Every request is a single JSON object on one line. Replies are also single-line JSON objects.

## Requests

### `get_state`

```json
{"id":1,"type":"get_state"}
```

Reply:

```json
{"id":1,"ok":true,"state":{...}}
```

### `subscribe`

```json
{"id":1,"type":"subscribe"}
```

Reply:

```json
{"id":1,"ok":true,"state":{...}}
```

After the initial reply, the connection receives state events:

```json
{"type":"event","event":"state","state":{...}}
```

### `set_workspace`

```json
{"id":1,"type":"set_workspace","workspace_id":3}
```

### `set_vout_focus`

By id:

```json
{"id":1,"type":"set_vout_focus","vout_id":2}
```

By output + name:

```json
{"id":1,"type":"set_vout_focus","output":"DP-1","vout_name":"right"}
```

### `move_workspace_to_vout`

By id:

```json
{"id":1,"type":"move_workspace_to_vout","workspace_id":3,"vout_id":2}
```

By output + name:

```json
{"id":1,"type":"move_workspace_to_vout","workspace_id":3,"output":"DP-1","vout_name":"right"}
```

Control replies use:

```json
{"id":1,"ok":true}
```

Errors use:

```json
{"id":1,"ok":false,"error":"..."}
```

## State Shape

The snapshot/event state is structured around:

- `pointer`: pointer location metadata for subscribers (for example reveal-hover state)
- `outputs`: physical outputs, geometry, active/focused state, active window info
- `virtual_outputs`: vout ids, names, workspace mapping, layout, regions
- `workspaces`: flat workspace list with visibility/focus/assignment metadata

`pointer` currently includes:

- `output`: output name under the pointer (or `null`)
- `reveal_hover`: compositor-provided hover state intended for bar/panel reveal automation
- `reveal_edge`: `top`, `bottom`, `left`, `right`, or `null`

Virtual outputs currently expose a single region, but the schema uses `regions[]` so it can represent spanning virtual outputs later without a schema break.

## CLI

`vwlctl` is the reference client:

```sh
vwlctl get-state
vwlctl subscribe
vwlctl set-workspace 3
vwlctl set-vout-focus --output DP-1 --vout right
vwlctl move-workspace-to-vout 3 --vout-id 2
```
