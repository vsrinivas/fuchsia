# Testing: Questions and Answers

You are encouraged to add your own questions (and answers) here!

[TOC]

## Q: How do I define a new unit test?

A: Use language-appropriate constructs, like GTest for C++. You can define a new
file if need be, such as:

(in a BUILD.gn file)

```code
executable("unittests") {
  output_name = "scenic_unittests"
  testonly = true
  sources = ["some_test.cc"],
  deps = [":some_dep"],
}
```

## Q: What ensures it is run?

A: An unbroken chain of dependencies that roll up to your `fx set` command's
universe of available packages (expandable using the
[`--with`](https://fuchsia.googlesource.com/fuchsia/+/master/tools/devshell/set)
flag), typically going through the `all` target of
`//<layer>/packages/tests/BUILD.gn`, such as
[`//garnet/packages/tests:all`](/garnet/packages/tests/BUILD.gn).

For example:

`//garnet/lib/ui/scenic/tests:unittests`

is an executable, listed under the "tests" stanza of

`//garnet/bin/ui:scenic_tests`

which is a package, which is listed in a package group

`//garnet/packages/tests:scenic`

which is itself included in the catch-all group,

`//garnet/packages/tests:all`

Your product definition (typically one found in
[products/](https://fuchsia.googlesource.com/fuchsia/+/master/products) may or
may not transitively include this test group. If it doesn't, add it to your `fx
set` command, like so:

`fx set ... --with //garnet/packages/tests:scenic_tests`

Typically, one just adds a new test to an existing binary, or a new test binary
to an existing package.

## Q: How do I run this unit test on a QEMU instance?

There's the easy way if your QEMU has networking, and the hard way if it
doesn't.

A (with networking): In one terminal, start your QEMU instance with `fx run -N`.
Next, on another terminal, type in `fx run-test scenic_tests`.

This invocation runs all the test executables in the `scenic_tests` package.

A (no networking): Start a QEMU instance (`fx run`), and then *manually* invoke
the `runtests` command.

In the QEMU shell, type in `runtests /pkgfs/packages/scenic_tests/0/test`. The
argument is a specific directory containing the test executables.

Note Well! Without networking, the files are loaded into the QEMU instance at
startup. So after rebuilding a test, you'll need to shutdown and re-start the
QEMU instance to see the rebuilt test.

To exit QEMU, `dm shutdown`.

## Q: How do I run this unit test on my development device?

A: Either manual invocation, like in QEMU, **or** `fx run-test` to a running
device.

Note that the booted device may not contain your binary at startup, but `fx
run-test` will build the test binary, ship it over to the device, and run it,
while piping the output back to your workstation terminal. Slick!

Make sure your device is running (hit Ctrl-D to boot an existing image) and
connected to your workstation.

From your workstation, `fx run-test scenic_tests` will serially run through all
test executables contained in the `scenic_tests` package.

To run just one test executable, `fx run-test scenic_test -t scenic_unittests`,
where the argument to `-t` is the executable name.

You can automatically rebuild, install, and run your tests on every source file
change with `fx -i`. For instance: `fx -i run-test scenic_tests`.

## Q: Where are the test results captured?

A: The output is directed to your terminal.

There does exist a way to write test output into files (including a summary JSON
file), which is how CQ bots collect the test output for automated runs.

## Q: How to disable a test? How to find and run disabled tests?

A: To temporary prevent a particular test to being run as part of a test suite,
you can mark it as disabled. Disabled tests are defined by having their name
prefixed with `DISABLED_`. One way to find them is therefore simply `git grep
DISABLED_`.

If running the test outputs `YOU HAVE 1 DISABLED TEST`, you can also pass the
following flags to find out which test is disabled: `fx run-test scenic_tests --
--gtest_list_tests --gtest_filter=*DISABLED_*`.

To force-run disabled tests: `fx run-test scenic_tests --
--gtest_also_run_disabled_tests`.

Alternatively, you may also want to disable an entire test *executable* within a
package containing several test executables. To do this, edit the `BUILD.gn` as
follows: `tests = [ { name = "scenic_unittests", disabled = true } ]`. As a
result, `scenic_unittests` will be put in a `disabled` sub-directory of
`/pkgfs/packages/<package_name>/0/test`, and will not be run by the CQ system.

## Q: How do I run a bunch of tests automatically? How do I ensure all dependencies are tested?

A: Upload your patch to Gerrit and do a CQ dry run.

## Q: How do I run this unit test in a CQ dry run?

A: Clicking on CQ dry run (aka +1) will take your CL's properly defined unit
test and run it on multiple bots, one for each build target (*x86-64* versus
*arm64*, *release* versus *debug*). Each job will have an output page showing
all the tests that ran.
