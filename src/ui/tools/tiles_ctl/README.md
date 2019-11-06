# `tiles_ctl`

`tiles_ctl` is a utility to control `tiles`, a very simple tiling view manager.
It operates by starting or connecting to the `tiles` component and sending it
commands through the `fuchsia.developer.tiles.Controller` FIDL interface.

## Commands

## `start`

Starts the `tiles` component

## `list`

Lists the current set of tiles including their key, url, current size and
focusability.

##  `add [--disable-focus] <url> [<args>...]`

Adds a new tile with the given component URL.
If `--disable-focus` is passed, the tile won't be able to gain focus.


## `remove <key>`

Removes the tile with the given key.

## `quit`

Tells the `tiles` component to quit.
