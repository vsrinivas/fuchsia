# Fuchsia Maxwell

Services to expose ambient and task-related context, suggestions and
infrastructure for leveraging MI.

## Running

Before running, follow the instructions at the Fuchsia
[README](https://fuchsia.googlesource.com/docs/+/HEAD/README.md).

Maxwell services are launched by Modular on startup with the `default` build
module.

## Testing

To run the Maxwell tests in a running Fuchsia environment:

    @ /system/test/maxwell_test

A spurious error like

    [ERROR:apps/modular/src/component_manager/resource_loader.cc(34)] Error from network service connection

may appear due to the tests not running with networking wired in; it is
immaterial to the test.

## Kronk

Kronk is the Assistant agent. It is built out-of-tree and deployed to a Google
Cloud bucket. Normally, Maxwell launches the stable release version of this
agent. For development, set the following in your `fgen` to use the HEAD
unstable version:

    fgen --args kronk_dev=true

(and then rebuild)
