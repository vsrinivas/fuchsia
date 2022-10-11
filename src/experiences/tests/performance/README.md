Workstation Performance Tests
============================

This folder houses all performance tests for the Workstation product configuration.

# Build

```shell
$ fx set workstation_eng.x64 --release --args=flutter_driver_enabled=true
```

# Run

```shell
$ fx test experiences_ermine_session_shell_performance_test
```
