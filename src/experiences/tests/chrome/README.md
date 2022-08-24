Chrome Smoke Test
============================

This folder houses Chrome smoke test for the Workstation Pro product configuration.

# Build

```
fx set workstation_eng.chromebook-x64 --release --args=flutter_driver_enabled=true

fx build
```

# Run
## Emulator

```
fx test --e2e -o workstation_chrome_smoke_test
```

## Atlas

### Prerequisite
- A paved Atlas - follow [this guide](https://fuchsia.dev/fuchsia-src/development/hardware/chromebook) if needed

```
fx serve-remote --tunnel-ports=9095,9096,9097,9098,9099

env FUCHSIA_PROXY_PORTS=9095,9096,9097,9098,9099 fx test -f -o --e2e workstation_chrome_smoke_test
```
