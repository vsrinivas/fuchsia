# sysmgr

Reviewed on: 2021-04-09

sysmgr is [documented here](/docs/concepts/components/v1/sysmgr.md).

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