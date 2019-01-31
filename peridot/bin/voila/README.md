# Voilà

**Status: WIP**

Voilà is a simulation harness that supports running stories across emulated
Fuchsia devices. "Emulation" is based on instantiating the framework components
responsible for running the story multiple times.

To run Voilà, run on device:

```
tiles_ctl start
tiles_ctl add fuchsia-pkg://fuchsia.com/voila#meta/voila.cmx
```

## Development

To run tests, run on host:

```
fx run-image-test voila_bin_test
```
