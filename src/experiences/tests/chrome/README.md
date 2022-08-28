Chrome Smoke Test
============================

This folder houses Chrome "smoke test" and "advanced smoke test" for the
Workstation Pro product configuration.

# Build

```shell
$ fx set workstation_eng.chromebook-x64 --release --args=flutter_driver_enabled=true

$ fx build
```

# Run
## Emulator

```shell
$ fx serve
$ fx test --e2e -o workstation_chrome_smoke_test
```

Note: The "advanced smoke test" launches a very limited local HTTP server, which
may not work correctly when using the emulator, with common networking
configurations.

## Google Pixelbook Go (atlas) Chromebook

Prerequisites:

- A paved atlas - follow
  [this guide](https://fuchsia.dev/fuchsia-src/development/hardware/chromebook)
  if needed

### Directly-connected
```shell
$ fx serve
$ fx test -f -o --e2e workstation_chrome_smoke_test
$ fx test -f -o --e2e workstation_chrome_advanced_smoke_test
```

### Remotely-connected
```shell
$ fx serve-remote --tunnel-ports=9095,9096,9097,9098,9099
$ env FUCHSIA_PROXY_PORTS=9095,9096,9097,9098,9099 fx test -f -o --e2e workstation_chrome_smoke_test
$ env FUCHSIA_PROXY_PORTS=9095,9096,9097,9098,9099 fx test -f -o --e2e workstation_chrome_advanced_smoke_test
```
