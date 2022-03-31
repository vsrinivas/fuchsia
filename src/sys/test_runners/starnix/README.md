# Starnix Test Runners

Reviewed on: 2021-04-14

Each Starnix test runner is a [test runner][test-runner] that launches its test components
using a Starnix runner.

These test runners are useful for running test binaries that are compiled for Linux.

Each Linux binary expects to run in a particular environment (e.g., different system images). Each
environment is represented by a different test runner component. For example, a test that wants to
run in an Android environment would use the `stardroid_test_runner`, while a test that wants to run
in a ChromiumOS environment would use `starmium_test_runner`.

An example component hierarchy looks as follows:

  * `starnix_test_runners.cm`: bundles all the different runners under one component, makes it
                               easier to add/remove test runners.
    * `stardroid_test_runner.cm`
      * `starnix_test_runner.cm`: the component that interacts with the test manager.
      * `starnix_runner.cm`: the component that actually runs the test binaries, from
	      	             a package that contains an Android system image.
    * `starmium_test_runner.cm`
      * `starnix_test_runner.cm`: the component that interacts with the test manager.
      * `starnix_runner.cm`: the component that actually runs the test binaries, from
	      		     a package that contains a ChromiumOS system image.
    * `starnix_unit_test_runner`: the component that runs starnix runner unit tests.

It's worthwhile to note that most of the `.cml` content and all of the code is the same between the
different runners. The only difference is which package the `starnix_runner.cm` is loaded from,
since the different packages contain different system images and configuration.

# Starnix Unit Test Runner

This runner is intended to be used by the starnix runner's unit tests. It is a
wrapper around the regular Rust test runner that adds additional permissions
required by starnix unit tests that shouldn't be available to regular Rust test
components.

[test-runner]: ../README.md