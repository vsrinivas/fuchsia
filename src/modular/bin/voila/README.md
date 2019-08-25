# Voilà

**Status: WIP**

Voilà is a simulation harness that supports running stories across emulated
Fuchsia devices. "Emulation" is based on instantiating the framework components
responsible for running the story multiple times.

## Running

To run Voilà, build the workstation product:

```
fx set workstation.x64
 \ --with peridot/packages/tools:voila
 \ [--with-base topaz/packages/examples:misc]
```

We recommend adding topaz misc examples to base so that some examples are
available to run in replicas. (try the modern classics: `todo_list` and
`mine_digger`)

Then, run on device:

```
tiles_ctl start
tiles_ctl add fuchsia-pkg://fuchsia.com/voila#meta/voila.cmx
```

## Development

To run tests, run on host:

```
fx run-test voila_tests
```
