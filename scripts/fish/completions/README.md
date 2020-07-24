# Fish Completion

Fish completion support for the `fx` tool. Subcommands, their short and long
options and descriptions for both are parsed from help documentation.

## Use

Sourcing the `fx-env.fish` will load completions for you:

```shell
source ~/fuchsia/scripts/fx-env.fish
```

## Completion Cache

First attempt at `fx` tab completion will hang while completions are
generated. Subsequent tab completions will be fast as they are cached by
fish. If you wish to regenerate this cache open a new shell or touch the
`fx.fish` file:

```shell
touch $FUCHSIA_DIR/scripts/fish/completions/fx.fish
```

## Advanced Usage

If you wish to use only the `fx.fish` file and avoid sourcing
`~/fuchsia/scripts/fx-env.fish` you can do the following:

1. Set `$FUCHSIA_DIR` to your fuchsia tree location. This can be done with a
universal environment variable.

```shell
set -U FUCHSIA_DIR $HOME/fuchsia
```

2. Add `fx.fish` to any completion directory as described in the
[fish manual](https://fishshell.com/docs/current/index.html#where-to-put-completions).
Valid locations are contained in the `$fish_complete_path` variable.

For example:

```shell
mkdir -p ~/.config/fish/completions/
ln -s ~/fuchsia/scripts/fish/completions/fx.sh ~/.config/fish/completions/
```
