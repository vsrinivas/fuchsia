Workstation End-to-End Tests
============================

This folder houses all end-to-end tests for the Workstation product configuration.

# Build

`fx set workstation_eng.x64 --release --args=flutter_driver_enabled=true`

# Run

`fx test --e2e -o ermine_session_shell_e2e_test`
