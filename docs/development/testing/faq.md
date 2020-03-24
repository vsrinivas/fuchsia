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
[`--with`](/tools/devshell/set)
flag), typically going through the `all` target of
`//<layer>/packages/tests/BUILD.gn`, such as
[`//garnet/packages/tests:all`](/garnet/packages/tests/BUILD.gn).

For example:

`//src/ui/scenic:scenic_unittests`

is an executable, listed under the "tests" stanza of

`//src/ui/scenic:scenic_tests`

which is a package, which is listed in a package group

`//garnet/packages/tests:scenic`

which is itself included in the catch-all group,

`//garnet/packages/tests:all`

Your product definition (typically one found in
[products/](/products) may or
may not transitively include this test group. If it doesn't, add it to your `fx
set` command, like so:

`fx set ... --with //garnet/packages/tests:scenic_tests`

Typically, one just adds a new test to an existing binary, or a new test binary
to an existing package.

## Q: How do I run this unit test on a QEMU instance?

There's the easy way if your QEMU has networking, and the hard way if it
doesn't.

A (with networking): In one terminal, start your QEMU instance with `fx qemu -N`.
Next, on another terminal, type in `fx test scenic_tests`.

This invocation runs all the test executables in the `scenic_tests` package.

A (no networking): Start a QEMU instance (`fx qemu`), and then *manually* invoke
the `runtests` command.

In the QEMU shell, type in `run-test-component scenic_tests`. The
argument is a specific directory containing the test executables.

Note Well! Without networking, the files are loaded into the QEMU instance at
startup. So after rebuilding a test, you'll need to shutdown and re-start the
QEMU instance to see the rebuilt test.

To exit QEMU, `dm shutdown`.

## Q: How do I run this unit test on my development device?

A: Either manual invocation, like in QEMU, **or** `fx test` to a running
device.

Note that the booted device may not contain your binary at startup, but `fx
test` will build the test binary, ship it over to the device, and run it,
while piping the output back to your workstation terminal. Slick!

Make sure your device is running (hit Ctrl-D to boot an existing image) and
connected to your workstation.

From your workstation, `fx test scenic_tests` will serially run through all
test executables contained in the `scenic_tests` package.

To run just one test executable, use the following command:

```bash
fx test <executable-name>
```

Note: To run a unit test, the deprecated `fx run-test` command used the `-t`
flag. To learn more about the new `fx test` command, see
[Running tests as components][running_tests_as_components].

You can automatically rebuild, install, and run your tests on every source file
change with `fx -i`. For instance: `fx -i test scenic_tests`.

## Q: Where are the test results captured?

A: The output is directed to your terminal.

There does exist a way to write test output into files (including a summary JSON
file), which is how CQ bots collect the test output for automated runs.

## Q: How to disable a test? How to find and run disabled tests? {#disable-test}

A: There are several ways to do this. Whenever doing any of these, be sure to
file a bug and reference that bug in a comment in the code that disables the
test.

### Tag the test as flaky

You can do this by adding "flaky" to the `tags` field in the
[test environment](/docs/concepts/testing/environments.md). This operates
on the entire test target (which corresponds to an executable). It will prevent this target
from running on the builders in the commit queue, and enable the target on special flaky
builders that continue to run the test in CI. Be sure to note the bug in a
comment in the BUILD.gn file.
[Example change](https://fuchsia-review.googlesource.com/c/topaz/+/296629/3/bin/flutter_screencap_test/BUILD.gn).

If you want to disable only some tests that are part of a larger test target, you'll
need to split the target into two GN targets, and tag one as flaky.

### C++ googletest only: Prefix name with DISABLED

To disable a particular test inside of a larger test executable,
you can mark it as disabled. Disabled tests are defined by having their name
prefixed with `DISABLED_`. One way to find them is therefore simply `git grep
DISABLED_`.

If running the test outputs `YOU HAVE 1 DISABLED TEST`, you can also pass the
following flags to find out which test is disabled: `fx run-test scenic_tests --
--gtest_list_tests --gtest_filter=*DISABLED_*`.

To force-run disabled tests: `fx run-test scenic_tests --
--gtest_also_run_disabled_tests`.

### Rust only: apply the `#[ignore]` attribute

To disable a particular test inside of a larger Rust test executable, you can
tag it with `#[ignore]`. It should be applied underneath the `#[test]` attribute.

Example:

```rust
#[test]
#[ignore] // TODO(fxbug.dev/NNNNN) re-enable this test when de-flaked
fn flaky_test_we_need_to_fix() { ... }
```

### Mark test disabled

Alternatively, you may also disable an entire test executable within a
package containing several test executables. To do this, edit the `BUILD.gn` as
follows: `tests = [ { name = "scenic_unittests", disabled = true } ]`. As a
result, `scenic_unittests` will be put in a `disabled` sub-directory of
`/pkgfs/packages/<package_name>/0/test`, and will not be run by the CQ system.

### Comment out the test

To disable a particular test inside of a larger test executable, you can comment
out the code that defines that test.

## Q: How do I run a bunch of tests automatically? How do I ensure all dependencies are tested?

A: Unlike the `fx run-test` command, the primary feature of `fx test` is batch
execution. See [Running tests as components][running_tests_as_components]
for examples on how to run multiple tests or test suites together.

Additionally, you can always upload your patch to Gerrit and do a CQ dry run.

## Q: How do I run this unit test in a CQ dry run?

A: Clicking on CQ dry run (aka +1) will take your CL's properly defined unit
test and run it on multiple bots, one for each build target (*x86-64* versus
*arm64*, *release* versus *debug*). Each job will have an output page showing
all the tests that ran.

## Q: How do I use some build time artifacts in my unit test?

A: The simplest artifact is just a file that is in your source directory.  For
this you just need to add it to `resources` attribute of the package definition
of your unit test.  For example, you may do something like this in your
`BUILD.gn`:

```code
rustc_binary("my-great-app") {
  with_unit_tests = true

  ...
}

test_package("my-great-app-tests") {
  deps = [
    ":my-great-app_test",
  ]

  resources = [
    {
      path = "source.zip"
      dest = "testing.zip"
    }
  ]
```

The file will be available as `/pkg/data/testing.zip` inside the environment
where the test binary will be executed.

TODO: If you want an artifact that is generated as part of the build process,
you should probably add the rule that generates the artifact to the `data_deps`
array of the `test_package` rule.  But I have not tried it yet.  Update this
section when you will try it :)

[running_tests_as_components]: /docs/development/testing/running_tests_as_components.md#running_multiple_tests
