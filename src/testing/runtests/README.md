# Runtests

Runtests is a command line tool that runs tests.

The most complete documentation of its capabilities and interface is the usage
string it prints.

It may be useful to developers working with the bringup
[product configuration](/products/README.md#bringup), since `fx test` does not
support running tests in that configuration. In this configuration, it can be
invoked from the serial console as `runtests`. To see a list of all the tests
are on the device's Boot FS, run `runtests --all -d`.

It is used in continuous integration infrastructure in the following ways:

*   In coverage profile builders it is used since it knows how to set up
    collection of coverage profiles. See
    [fuchsia-run-test.cc](/zircon/system/ulib/runtests-utils/fuchsia-run-test.cc).
*   In bringup builders it is used to run one test at a time. It's useful (vs
    just invoking the tests directly) because it enforces timeouts and
    translates exit codes into predictable strings that are written to the
    serial console, which are then interpreted by testrunner.
