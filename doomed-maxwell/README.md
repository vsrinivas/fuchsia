Fuchsia Maxwell
===============

Services to expose ambient and task-related context, suggestions and
infrastructure for leveraging MI.

A note on naming: as of the time of this writing, mojom was reported to have an
issue with hyphenated names, so we've used underscores.

Running
-------

Before running, follow the instructions at [manifest/README.md](https://fuchsia.googlesource.com/manifest/+/master/README.md).

Cheat sheet (assuming fuchsia under ~; aliases might be convenient):

* When file structure changes:

      ~/fuchsia/packages/gn/gen.py --goma

* To build:

      ~/fuchsia/buildtools/ninja -j1000 -C ~/fuchsia/out/debug-x86-64

* To run Fuchsia:

      ~/fuchsia/magenta/scripts/run-magenta-x86-64 -x ~/fuchsia/out/debug-x86-64/user.bootfs

* To exit Fuchsia:

      <ctrl-a x>

To run the Maxwell tests in a running Fuchsia environment:

    application_manager mojo:maxwell_test

There is no way to quit application_manager without shutting down Fuchsia at
this time.
