# Fuchsia Maxwell

Services to expose ambient and task-related context, suggestions and
infrastructure for leveraging MI.

## Running

Before running, follow the instructions at the Fuchsia
[README](https://fuchsia.googlesource.com/docs/+/HEAD/README.md).

Maxwell services are launched by Modular on startup with the `default` build
module.

## Fuchsia Intelligence Dashboard

Maxwell serves a diagnostic dashboard on port 4000. To access it on most Fuchsia
development setups (including EdgeRouter and QEMU), watch the startup logs for
the DHCP announcement. Point your browser to port 4000 of that IP after logging
into Fuchsia.

At present, the dashboard includes a context dump. More features to follow.

## Testing

To run the Maxwell integration tests in a running Fuchsia environment:

    @ /system/test/maxwell/integration

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

To suppress Kronk startup, use:

    fgen --args kronk=false

## MI Dashboard

The MI dashboard provides an off-device view into the data used by the
Maxwell sub-system through a web page.  Its web server starts on port 4000.
To disable the dashboard, for example while running tests, you can set the
following parameter via `fgen`:

    fgen --args mi_dashboard=false

## Other Docs

* [Module Manifest](docs/module_manifest.md)
