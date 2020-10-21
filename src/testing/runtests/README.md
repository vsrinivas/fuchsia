# Runtests

Runtests is a command line tool that runs tests.

The most complete documentation of its capabilities and interface is the usage
string it prints.

It may be useful to developers working with the bringup
[product configuration](/products/README.md#bringup), since currently `fx test`
does not support running tests in that configuration. In this configuration, it
should can be invoked from the serial console as `runtests`. To see a list of
all the tests are on the device's Boot FS, run `runttests --all -d`.

It is used in continuous integration infrastructure in the following ways:

*   In coverage profile builders it is used since it knows how to set up
    collection of coverage profiles.
*   In bringup builders it is used with `--all`.
*   In bringup "test in shards" builders (planned replacement for the above
    bringup builders, tracked by <https://fxbug.dev/59963>), it is used to run
    one test at a time. It's useful (vs just invoking the tests directly)
    because it enforces timeouts and translates exit codes into predictable
    strings that are written to the serial console.
