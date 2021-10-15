Chrome Smoke Test
============================

This folder houses Chrome smoke test for the Workstation Pro product configuration.

# Build

`fx set workstation_pro.chromebook-x64 --release --args=flutter_driver_enabled=true`

# Run

`fx test -o --e2e workstation_chrome_smoke_test`
