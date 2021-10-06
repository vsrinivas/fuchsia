# Configurator

This component interfaces with drivers and configures them according to the expectations of the
particular procuct/board in use if the product/board is supported.

## Building

To add this component to your build, append
`--with-base src/media/audio/boards/configurator`
to the `fx set` invocation.

## Running

Use `ffx component run` to launch this component into a restricted realm
for development purposes:

```
$ ffx component run fuchsia-pkg://fuchsia.com/configurator#meta/configurator.cm
```


