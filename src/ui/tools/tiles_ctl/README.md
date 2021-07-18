# `tiles_ctl`

`tiles_ctl` is a utility to control `tiles`, a very simple tiling view manager.
It operates by starting or connecting to the `tiles` component and sending it
commands through the `fuchsia.developer.tiles.Controller` FIDL interface.

## Commands

### `start`

Starts the `tiles` component

### `list`

Lists the current set of tiles including their key, url, current size and
focusability.

###  `add [--disable-focus] <url> [<args>...]`

Adds a new tile with the given component URL.
If `--disable-focus` is passed, the tile won't be able to gain focus.


### `remove <key>`

Removes the tile with the given key.

### `quit`

Tells the `tiles` component to quit.

## Flatland

Adding the argument `--flatland` to any of the commands above causes `tiles_ctl` to talk to the `tiles-flatland` servcie instead of the `tiles` service.  When adding a tile, the URL should refer to n app that knows how to be embedded in a Flatland scene graph.

### Limitations
[fxbug.dev/80883](http://fxbug.dev/80883): `tiles-flatland` connects directly to the display, not RootPresenter.  Therefore:
- `tiles-flatland` cannot be running at the same time as RootPresenter
- embedded tile apps will not receive input
