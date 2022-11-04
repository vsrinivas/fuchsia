# Graphic System Validation Test

The directory contains system validation tests for graphic. The tests use existing example apps created to demonstrate how to render views on the screen.

System validation sets up the test with following capability routing relationships:

```
test_manager <-- root
    |  parent to
    V
system_validation_test_realm (facet: system-validation) <-- system validation test root
    |  parent to
    V
test component (`ui_app_instrumentor.rs`)
    |  parent to
    V
sample-app (ex: `flatland-view-provider.cm`)
```

# Run existing tests

1. Build `workstation_eng_paused` product with system validation test targets.

```
fx set workstation_eng_paused.qemu-x64 --release --with-base //sdk/bundles:tools  --with //src/testing/system-validation:tests
fx build
```

2. Start the emulator and package server

```
ffx emu start
fx serve
```

3. Run test

```
fx test simplest_sysmem_system_validation
fx test spinning_square_system_validation
fx test flatland_view_provider_system_validation --ffx-output-directory /path/to/output/dir
```
