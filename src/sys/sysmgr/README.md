# sysmgr

Reviewed on: 2020-03-19

sysmgr is one of the two major pieces of the v1 Component Runtime (appmgr being
the other). It is responsible for hosting the 'sys' realm/environment that
contains 'global' system services. Most components on Fuchsia today still run
under the 'sys' realm (until they are migrated to the newer v2 Component
Runtime) though there are many components run in child realms under 'sys', such
as components launched and managed by the Modular framework.

This application runs quite early in the Fuchsia boot process. See the [boot
sequence](/docs/concepts/framework/boot_sequence.md)
for more information.

sysmgr is designed to be fairly robust. If any of the services dies, they
will be restarted automatically the next time an application attempts to
connect to that service.

By default, sysmgr reads all configuration files from `/config/data/`. The
configuration format as well as instructions for adding to the sysmgr
configuration are [documented here](sysmgr-configuration.md).

## Building

This project is typically included in Fuchsia builds by default, but it can be
added to a build by adding `--with //src/sys/sysmgr` to the `fx set`
invocation.

## Running

Sysmgr will be running on any system with [appmgr](../appmgr/README.md).

## Testing

Config unit tests are available in the `sysmgr_tests` package.

```
$ fx test sysmgr_tests
```

Integration tests are available in the `sysmgr-integration-tests` package.

```
$ fx test sysmgr-integration-tests
```

## Source layout

The entrypoint is located in `main.cc`, and all code lives in top-level `.cc`
and `.h` files.
