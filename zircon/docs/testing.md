# Testing

## Quick Start

To build Zircon and run unit tests, run one of the following commands:

```sh
# Build and run x64.
./scripts/build-zircon-x64 && ./scripts/run-zircon-x64

# Build and run arm64.
./scripts/build-zircon-arm64 && ./scripts/run-zircon-arm64
```

Once the scripts finish running, you should see the Zircon shell. To run
userspace tests, use the Zircon shell to run:

```sh
runtests
```

To run in-kernel tests, use the Zircon shell to run:

```sh
k ut all
```

The [Notes for hacking on Zircon](hacking.md) page has more details about how to
use the Zircon shell and how to automatically build all supported architectures.

## Userspace Tests

The test harness, runtests, picks up and runs all of the executables from the
`/boot/test` and `/system/test` directories. If you provide a command-line
argument, such as `runtests -S -m widget_test`, runtests will only run the
single test requested -- in this case, `widget_test`.

"runtests" takes command-line arguments to toggle classes of tests to execute.

These classes are the following:

* **Small**: Isolated tests for functions and classes. These must be totally
  synchronous and single-threaded. These tests should be parallelizable; there
  shouldn't be any shared resources between them.
* **Medium**: Single-process integration tests. Ideally these are also synchronous
  and single-threaded but they might run through a large chunk of code in each
  test case, or they might use disk, making them a bit slower.
* **Large**: Slow, multi-process, or particularly incomprehensible single-process
  integration tests. These tests are often too slow / flaky to run in a CQ, and
  we should try to limit how many we have.
* **Performance**: Tests which are expected to pass, but which are measured
  using other metrics (thresholds, statistical techniques) to identify
  regressions.

Since runtests doesn't really know what "class" is executing when it launches a
test, it encodes this information in the environment variable
`RUNTESTS_TEST_CLASS`, which is detailed in [the unittest
header][unittest-header] , and lets the executable itself decide what to run /
not run. This environment variable is a bitmask indicating which tests to run.

For example, if a a test executable is run with "small" and "medium" tests,
it will be executed ONCE with `RUNTESTS_TEST_CLASS` set to 00000003 (the
hex bitwise OR of "TEST_SMALL" and "TEST_MEDIUM" -- though this information
should be parsed using the [unittest header][unittest-header], as it may be
updated in the future).

### Zircon Tests (ulib/test, and/or using ulib/unittest)

The following macros can be used to filter tests into these categories:
```
RUN_TEST_SMALL(widget_tiny_test)
RUN_TEST_MEDIUM(widget_test)
RUN_TEST_LARGE(widget_big_test)
RUN_TEST_PERFORMANCE(widget_benchmark)
```

The legacy `RUN_TEST(widget_test)` is aliased to mean the same thing as
`RUN_TEST_SMALL`.

### Fuchsia Tests (not using ulib/unittest)

The environment variable `RUNTESTS_TEST_CLASS` will still be available to all
executables launched by runtests. The [unittest header][unittest-header] can be
used to parse different categories of tests which the runtests harness attempted
to run.

### Runtests CLI

By default, runtests will run both small and medium tests.

To determine how to run a custom set of test categories, run `runtests -h`,
which includes usage information.

[unittest-header]: ../system/ulib/unittest/include/unittest/unittest.h "Unittest Header"


## Kernel-mode Tests

The kernel contains unit tests and diagnostics, which can be run using the `k`
command. The output of the `k` command will only be shown on the
console. Depending on your configuration, this might be the serial console, or
the `debuglog` virtual terminal.

### Unit tests

Many parts of the kernel have unit tests, which report success/failure
automatically. These unit tests are built using the primitives provided by [the
kernel unit-test library](../kernel/lib/unittest). You can find these statically
by searching for `UNITTEST_START_TESTCASE`.

These tests can be run from the shell with `k ut`. `k ut all` will run all tests
or you can use `k ut $TEST_NAME` to run a specific test.

### Diagnostics

Many parts of the kernel provide diagnostics, whose output requires manual
inspection. Some of these diagnostics are used to verify correctness
(e.g. [`timer_diag`](../kernel/tests/timer_tests.cpp)), while others simply
stress test a part of the system
(e.g. [`timer_stress`](../kernel/tests/timer_tests.cpp)).

To run a diagnostic, simply pass its name to the `k` command. For example, to
run the kernel's [builtin benchmarks](../kernel/tests/benchmarks.cpp), run `k
bench`. To find the full set of kernel diagnostics statically, search for
`STATIC_COMMAND`. To enumerate them dynamically, run `k help`.

Diagnostic tests are intended to be run via serial console, or with physical
access to the system. Some diagnostics may be destructive, and leave the system
in a broken state.
