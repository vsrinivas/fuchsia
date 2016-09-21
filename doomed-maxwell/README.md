Fuchsia Maxwell
===============

Services to expose ambient and task-related context, suggestions and
infrastructure for leveraging MI.

A note on naming: as of the time of this writing, mojom was reported to have an
issue with hyphenated names, so we've used underscores.

Running
-------

Before running, follow the instructions at [manifest/README.md](https://fuchsia.googlesource.com/manifest/+/master/README.md).

To run the Maxwell tests in a running Fuchsia environment:

    application_manager mojo:maxwell_test

There is no way to quit application_manager without shutting down Fuchsia at
this time.
