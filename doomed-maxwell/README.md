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

To disable the dashboard, for example while running tests, you can create your
own Maxwell startup config file and set the parameter `mi_dashboard` to
`false`.  The `maxwell` process accepts a `--config` argument with the path to
the JSON file.

See `src/user/default_config.json` for a starting point.

## Testing

To run the Maxwell integration tests in a running Fuchsia environment:

    @ /system/test/maxwell/integration

A spurious error like

    [ERROR:apps/modular/src/component_manager/resource_loader.cc(34)] Error from network service connection

may appear due to the tests not running with networking wired in; it is
immaterial to the test.

## Other Docs

* [Module Manifest](docs/module_manifest.md)
