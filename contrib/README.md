# Contrib

Helper scripts and integration snippets that are useful with `vwl` but are not required by the compositor itself.

## Waybar

- `waybar/vwl-reveal`: auto hide/reveal helper driven by `vwlctl subscribe`.

Install:

```sh
install -Dm755 contrib/waybar/vwl-reveal ~/.config/waybar/scripts/vwl-reveal
```

Run it from your session startup (for example `~/.config/vwl/run`) after starting Waybar:

```sh
waybar &
sh ~/.config/waybar/scripts/vwl-reveal >/dev/null 2>&1 &
```

Waybar config requirements:

- `"mode": "hide"`
- `"start_hidden": true`

