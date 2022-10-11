Ermine development shell for Fuchsia.

## Build

Use the following `fx set` command to build the workstation product:

```bash
fx set workstation.<board>
```

Where board can be: x64 | chromebook-x64

## Test

```bash
fx test ermine_unittests
```
