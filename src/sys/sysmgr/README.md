# sysmgr

Reviewed on: 2019-07-22

This directory contains sysmgr, an application which is responsible for
setting up an environment which provides access to global system services.

This application runs quite early in the Fuchsia boot process. See the [boot
sequence](/docs/concepts/framework/boot_sequence)
for more information.

sysmgr is designed to be fairly robust. If any of the services dies, they
will be restarted automatically the next time an application attempts to
connect to that service.

By default, sysmgr reads all configuration files from `/config/data/sysmgr/`,
which have one of the formats detailed [here](sysmgr-configuration.md).

Additional configurations can be contributed to `sysmgr` using the
`config_data()` template defined in `//build/config.gni`.

## Building

This project is typically included in Fuchsia builds by default, but it can be
added to a build by adding `--with //src/sys/sysmgr` to the `fx set`
invocation.

## Running

Sysmgr will be running on any system with [appmgr](../appmgr/README.md).

## Testing

Config unit tests are available in the `sysmgr_tests` package.

```
$ fx run-test sysmgr_tests
```

Integration tests are available in the `sysmgr_integration_tests` package.

```
$ fx run-test sysmgr_integration_tests
```

## Source layout

The entrypoint is located in `main.cc`, and all code lives in top-level `.cc`
and `.h` files.
