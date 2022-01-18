# Echo Example

This directory contains a simple example using program arguments and environment
variables in [Component Framework](/docs/concepts/components/introduction.md).

## Building

If these components are not present in your build, they can be added by
appending `--with //examples` to your `fx set` command. For example:

```bash
$ fx set core.x64 --with //examples --with //examples:tests
$ fx build
```

## Running

Use `ffx component run` to launch this component into a restricted realm
for development purposes:

-  **C++**

```bash
ffx component run fuchsia-pkg://fuchsia.com/echo-example#meta/echo_cpp.cm
```

-  **Rust**

```bash
$ ffx component run fuchsia-pkg://fuchsia.com/echo-example#meta/echo_rust.cm
```

When the above command is run, you can see the following output with `fx log`:

```
[echo] INFO: Hello, Alice, Bob, Spot!
```

## Testing

Unit tests for echo are available in the `echo-unittests` package.
Use the `ffx test run` command to run the tests on a target device:

-  **C++**

```bash
$ ffx test run fuchsia-pkg://fuchsia.com/echo-cpp-unittests#meta/echo-cpp-unittests.cm
```

You should see each of the unit tests execute and pass:

```
[RUNNING]       EchoTest.TestGreetOne
[PASSED]        EchoTest.TestGreetOne
[RUNNING]       EchoTest.TestGreetTwo
[PASSED]        EchoTest.TestGreetTwo
[RUNNING]       EchoTest.TestGreetThree
[PASSED]        EchoTest.TestGreetThree

3 out of 3 tests passed...
fuchsia-pkg://fuchsia.com/echo-cpp-unittests#meta/echo-cpp-unittests.cm completed with result: PASSED
```

-  **Rust**

```bash
$ ffx test run fuchsia-pkg://fuchsia.com/echo-rust-unittests#meta/echo-rust-unittests.cm
```

You should see each of the unit tests execute and pass:

```
Running test 'fuchsia-pkg://fuchsia.com/echo-rust-unittests#meta/echo-rust-unittests.cm'
[RUNNING]	tests::test_greet_one
[RUNNING]	tests::test_greet_two
[RUNNING]	tests::test_greet_three
[PASSED]	tests::test_greet_one
[PASSED]	tests::test_greet_two
[PASSED]	tests::test_greet_three

3 out of 3 tests passed...
```

