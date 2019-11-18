# appmgr

Reviewed on: 2019-07-22

Appmgr is responsible for launching v1 components and managing the namespaces in
which those components run. It is the first process started in the `fuchsia` job
by `devmgr`.

See:

* [Boot sequence](/docs/concepts/framework/boot_sequence)
* [Sandboxing](/docs/concepts/framework/sandboxing)
* [Package metadata](/docs/concepts/storage/package_metadata)

## Building

This project is typically included in Fuchsia builds by default, but it can be
added to a build by adding `--with //src/sys/appmgr` to the `fx set`
invocation.

## Running

Appmgr is run on all non-bringup Fuchsia builds. It can be interacted with via
the [`fuchsia.sys` FIDL apis](/sdk/fidl/fuchsia.sys).

## Testing

Unit tests for appmgr are available in the `appmgr_unittests` package.

```
$ fx run-test appmgr_unittests
```

Integration tests are available in the following packages, and each can be run
with `fx run-test`.

- `appmgr_integration_tests`
- `build_info_tests`
- `components_binary_tests`
- `has_deprecated_shell`
- `has_isolated_cache_storage`
- `has_isolated_persistent_storage`
- `has_isolated_temp`
- `has_shell_commands`
- `has_system_temp`
- `inspect_integration_tests`
- `inspect_vmo_integration_tests`
- `isolated_persistent_storage`
- `multiple_components`
- `no_isolated_temp`
- `no_persistent_storage`
- `no_services`
- `no_shell_commands`
- `no_shell`
- `no_system_temp`
- `some_services`
- `uses_system_deprecated_data`

## Source layout

The entrypoint is located in `main.cc`, with the majority of the implementation
living in top-level `.cc` and `.h` files, with the exception of the hub
implementation which is in `hub/`. Unit tests are located in `_unittest.cc`
files.

Integration tests are in the `integration_tests/` directory.
