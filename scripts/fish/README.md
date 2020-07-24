# Fish Shell

## Quickstart

Inside a fish shell run:

```shell
source ~/fuchsia/scripts/fx-env.fish
```

`fx-env.fish` can be sourced within the shell as needed or added to your fish
config. You can add it to your config with:

```shell
mkdir -p ~/.config/fish/conf.d/
echo 'source ~/fuchsia/scripts/fx-env.fish' >> ~/.config/fish/conf.d/fuchsia.fish
```

Replace `~/fuchsia` above with the location of your fuchsia checkout.
