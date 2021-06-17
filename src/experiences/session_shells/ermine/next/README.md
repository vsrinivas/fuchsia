Ermine.NEXT development shell for Fuchsia.

## Build

Use the following `fx set` command to build the workstation product:

```bash
fx set workstation.x64 --with-base=//src/experiences/session_shells/ermine/next:shell_config
```

## Test

```bash
fx test next_unittests
```
