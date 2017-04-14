Fuchsia Maxwell
===============

Services to expose ambient and task-related context, suggestions and
infrastructure for leveraging MI.

Running
-------

Before running, follow the instructions at the Fuchsia
[README](https://fuchsia.googlesource.com/docs/+/HEAD/README.md).

To run the Maxwell tests in a running Fuchsia environment:

    @ /system/apps/maxwell_test

Kronk
-----

Kronk is the Assistant agent. It is built out-of-tree and deployed to a Google
Cloud bucket. Normally, Maxwell launches the stable release version of this
agent. For development, set the following in your `fgen` to use the HEAD
unstable version:

    fgen --args kronk_dev=true

(and then rebuild)
