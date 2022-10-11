# ermine_testserver

## Build

```shell
$ fx set workstation_eng.x64 --with //src/experiences/bin/ermine_testserver
$ fx build
```

## Launch (as a visible session `Element`)

```shell
$ fx shell session_control add fuchsia-pkg://fuchsia.com/ermine_testserver#meta/ermine_testserver.cm
```

## Terminate

```shell
$ ffx component show ermine_testserver

# Copy the full "Moniker"

$ ffx component destroy <moniker>
```
