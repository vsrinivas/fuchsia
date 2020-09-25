# Fuchsia Testability Rubrics

## Goals

### Goals of this document

Fuchsia Testability is a form of Readability that focuses on ensuring that
changes to Fuchsia introduce testable and tested code.

As with any Readability process, the guidelines and standards are best made
publicly available, for reviewers and reviewees. This document is the Fuchsia
Testability equivalent of a style guide for a programming language readability
process.

It’s valuable to apply the Testability standards consistently, hence it is
important that anyone involved in Testability should study the doc (whether
you’re reviewing changes or authoring them).

The exact contents of the doc may change over time.

### Your goals as a Testability reviewer

*   **Determine if the change is tested.** Apply Testability-Review+1 if you
    agree that it’s tested, and reply with a note for what’s missing if it’s not.
*   Focus on whether the change is tested, not necessarily on what the change
    actually does. For instance you may apply Testability+1 if the change is
    well tested and at the same time Code-Review-1 if you would not like to see
    the change merged for other reasons.
*   Apply the standard (this doc) consistently.
*   For your own changes, it is okay to self Testability-Review+1 provided that
    the change clearly follows this rubric. If in doubt, seek approval from
    another testability reviewer.
*   If the change needs to be amended to meet the standards, provide actionable
    feedback.
*   Promote Fuchsia testing & testability.
*   Identify cases not handled by this doc and propose changes.
*   **Uphold the standard** but also **apply your good judgement**. The goal is
    to improve quality and promote a culture of testing. You’re not expected to
    execute a precise decision algorithm.

### Your goals as a change author

*   Inform yourself of the standards by reading this doc (you’re doing it right
    now!).
*   Respect that your Testability reviewer is volunteering for the role, and
    treat them appropriately.
*   Consider feedback given for ways that you could improve confidence in your
    change using testing.

## What to test? How to test?

*   **Changes to functionality should have a test that would have failed without
    said change.**
*   **Tests must be local to the code being changed: dependencies with test
    coverage do not count as test coverage.** For example, if "A" is used by a
    "B", and the "B" contains tests, this does not provide coverage for "A".
    If bugs are caught with "B"'s tests, they will manifest indirectly, making
    them harder to pinpoint to "A". Similarly, if "B" is deprecated (or just
    changes its dependencies) all coverage for "A" would be lost.
*   **Tests must be automated (CI/CQ when supported)**. A manual test is not
    sufficient, because there is no guarantee that a future change to the code
    (especially when authored by another engineer) will exercise the same manual
    tests. Exceptions may apply to some parts of the codebase to recognize
    ongoing automation challenges.
*   **Tests must minimize their external dependencies**. Our test infrastructure
    explicitly provisions each test with certain resources, but tests are able
    to access more than those that are provisioned. Examples of resources
    include hardware, CPU, memory, persistent storage, network, other IO
    devices, reserved network ports, and system services. The stability and
    availability of resources that are not provisioned explicitly for a test
    cannot be guaranteed, so tests that access such resources are inherently
    flaky and / or difficult to reproduce. Tests must not access external
    resources beyond the control of the test infrastructure. For example, tests
    must not access services on the Internet. Tests should only use resources
    beyond those provisioned explicitly for that test when necessary. For
    example, tests might have to access system services that do not have test
    doubles available. A small number of exceptions to this rule are made for
    end-to-end tests.
*   **Changes to legacy code** (old code that predates Testability requirements
    and is poorly tested) must be tested. Proximity to poorly-tested code is not
    a reason to not test new code. Untested legacy code isn’t necessarily old
    and crufty, it may be proven and battle-hardened, whereas new code that
    isn’t tested is more likely to fail!
*   **Changes you are making to someone else’s code** are subject to the same
    Testability requirements. If the author is changing code they’re not
    familiar with or responsible for, that’s more reason to test it well. The
    author can be expected to work with the responsible individual or team to
    find effective ways to test the change. Individuals responsible for the code
    under change are expected to help the author with testability with the same
    priority as the author’s change.

## What does not require testing

Missing testing coverage for the below should not prevent a change from
receiving Testability+1.

*   **Logging.** In most cases, it’s probably not worth testing the log output
    of components. The log output is usually treated as opaque data by the rest
    of the system, which means changes to log output are unlikely to break other
    system. However, if the log output is load bearing in some way (e.g.,
    perhaps some other system depends on observing certain log messages), then
    that contract is worth testing. This can also apply to other forms of
    instrumentation, such as Tracing. This does not apply to instrumentation
    when it is used as a contract, for instance Inspect usage can be tested, and
    should be if you rely on it working as intended (for instance in fx iquery
    or feedback reports).
*   **Code that we don’t own** (the source of truth is not in Fuchsia tree).
    Changes that pick up an update to source code that’s copied from elsewhere
    don’t bear testability requirements.
*   **Pure refactors** (changes that could have entirely been made by an
    automated refactor tool), such as moving files, renaming symbols, or
    deleting them, don’t bear testability requirements. Some languages can have
    behavior that’s exposed to such changes (e.g. runtime reflection), so
    exceptions may apply.
*   **Generated code.** Changes that are generated by tools (such as formatting,
    or generated code checked in as a golden file) don’t bear testability
    requirements. As an aside, it’s generally discouraged to check in generated
    code (rather harness the tools and have the code be generated at build
    time), but in the exceptional case don’t require tests for code written by
    machines.
*   **Testability bootstrapping.** In cases where the change is in preparation
    for introducing testability to the code, and this is explicitly documented
    by the author, then Testability reviewers may exercise discretion and take
    an IOU.
*   **Manual tests.** Manual tests are often themselves used to test or
    demonstrate functionality that is hard to test in an automated fashion.
    Additions or modifications to manual tests therefore do not require
    automated tests. However, it is strongly recommended that manual tests be
    paired with a README.md or TESTING.md document describing how to run them.
*   **Hardcoded values.** Additions or changes to hardcoded values do not
    necessarily require tests. Oftentimes, these values control behaviors that
    are not easily observable, such as unexposed implementation
    details, heuristics, or "cosmetic" changes (e.g. background color of a UI).
    Tests of the style `assert_eq!(CONFIG_PARAM, 5);` are not considered useful
    and are not required by testability. However, if the CL results in an easily
    observable behavioral change, the CL should include a test for the new
    behavior.

## What does require testing

### Recommended: Test for flakiness (if supported)

This is currently recomended but will become required once
<http://fxbug.dev/50483> is done.

Note: This feature is not currently supported for bringup builders.

As a testability reviewer, if a change adds or modifies tests, you
should make sure the author correctly tests for flakiness using the MULTIPLY
feature as described below. Check to see if it worked by clicking on the tryjob
and looking for a step that says `shard multiplied:<shard name>-<test name>`.
For example:

![multiplied shard screenshot](multiplied-shard-screenshot.png)

As a change author, when you add or modify tests, you should tell the
infrastructure to run those tests multiple times with a MULTIPLY field in the
commit message. You would add something like this to your commit message:

```txt
MULTIPLY: test_name (os): run_count
```

For example:

```txt
MULTIPLY: foo_tests (fuchsia): 30
```

Note: "os" and "run_count" are both optional; see [below](#multiply-examples)
for more examples.

Then do a CQ dry run (or choose a tryjob that runs your tests).
These tests show as separate shards for each test, which run that test as
many times as the specified run count. The timeout for running these tests
is 40 minutes on most builders. If a test takes too long, the shard may
time out.

The test name can be any of the following:

* The test package URL (for fuchsia tests) or path (for host tests). This is
  the name that Flake Fetcher uses to refer to tests, and is seen in the
  "name" field of each entry in `out/default/tests.json`. That file is
  created after you run `fx set` inside of your Fuchsia directory.
* A regular expression (using Go's [regular expression
  syntax](https://github.com/google/re2/wiki/Syntax)) that matches the test
  name as described above. However, note that if a single multiplier matches
  more than 5 different tests, it will be rejected (to prevent accidental
  DoSing). If this happens to you, simply edit your commit message locally or
  in the Gerrit UI to make your regular expression more specific.

The "os" field, if specified, should be either "fuchsia", "linux", or "mac".
If left unset, the multiplier will match any test, regardless of the test's
operating system, as long as the name matches.

If "run_count" is left unspecified, the infrastructure will use historical
test duration data to calculate a number of runs that will produce a single
multiplied test shard whose duration is similar to the expected duration of
the other shards (although the calculated run count will be limited to a
maximum of 2000). Longer tests will be run fewer times, shorter tests more
times.

Note: If your CL increases a test's duration, then the historical duration
data may no longer be accurate and the number of runs calculated by the
infrastructure may cause the shard to time out. In this case, you'll have to
edit the commit message and specify a lower number of runs.

#### Syntax examples {#multiply-examples}

* Title-case "Multiply" can be used instead of all-caps "MULTIPLY":

  ```txt
  Multiply: foo_tests (fuchsia): 30
  ```

* If you leave out the OS, the multiplier will be applied to any test that
  matches the multiplier name, regardless of OS:

  ```txt
  Multiply: foo_tests: 30
  ```

* If you leave out the number of runs, the infrastructure will calculate a
  number of runs that will fill up exactly one shard:

  ```txt
  Multiply: foo_tests (linux)
  ```

* You can also leave out both the OS and the number of runs:

  ```txt
  Multiply: foo_tests
  ```

* To multiply more than one test, add extra "Multiply" lines:

  ```txt
  Multiply: foo_tests
  Multiply: bar_tests
  ```

* Comma-separated multipliers in a single line are also supported:

  ```txt
  Multiply: foo_tests: 5, bar_tests (fuchsia): 6
  ```

* You can reference fuchsia tests by package URL and host tests by path:

  ```txt
  Multiply: fuchsia-pkg://fuchsia.com/foo_tests#meta/foo_tests.cmx
  Multiply: host_x64/bar_tests
  ```

* Regex and substring matching is also supported:

  ```txt
  Multiply: fuchsia.com/foo_tests
  ```

* This JSON syntax is also valid:

  ```json
  Multiply: `[
    {
      "name": "foo_bin_test",
      "os": "fuchsia",
      "total_runs": 30
    }
  ]`
  ```

### Tests should not sleep

Sleeps can lead to flaky tests because timing is difficult to control across
different test environments.  Some factors that can contribute to this
difficulty this are the test target's CPU speed, number of cores, and system
load along with environmental factors like temperature.

*   Avoid something like:

    ```c++
    // Check if the callback was called.
    zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));
    EXPECT_EQ(true, callback_happened);
    ```

*   Instead, explicitly wait for the condition:

    ```c++
    // In callback
    callback() {
        sync_completion_signal(&event_);
    }

    // In test
    sync_completion_wait(&event_, ZX_TIME_INFINITE);
    sync_completion_reset(&event_);
    ```

    This code sample was adapted from [task-test.cc](https://fuchsia-review.googlesource.com/c/fuchsia/+/326106/7/src/camera/drivers/hw_accel/ge2d/test/task-test.cc#48).

### Regression tests for race conditions

It is difficult to write regression tests for race conditions that don't have a high
false-pass rate. If you can write a test that deterministically reproduces the issue,
you should do that. Otherwise, if the data race was fixed by improving the locking
scheme used, you can add thread annotations as a regression test. For other races,
you should attempt to design APIs that prevent the existence of the race condition.

## Recently removed exemptions

*   **Engprod scripts** (e.g. `fx` commands) and associated configuration files**
    no longer have an exemption from testability. `fx` must have integration
    tests before further changes land. Exceptions may be granted by the fx team
    after consulting with a testability reviewer.

## Temporary testability exemptions

The following are currently exempt from Testability, while ongoing work aims to
change that.

*   **Engprod scripts** in the tools/devshell/contrib and associated
    configuration are exempt.
*   **GN templates** are not easily testable. We are working on a test framework
    for GN templates. Until then, it's permitted for build template changes to
    be manually tested only.
*   **Resource leaks** are not easily preventable in C-style code. In the longer
    term, such code should be refactored to use Rust or modern C++ idioms to
    reduce the chances of leaks, and automation should exist that is capable of
    automatically detecting leaks.
*   **Gigaboot** is a UEFI bootloader in //zircon/bootloader that predates
    testability policy. At present there is not an infrastructure available
    to write integration tests for the UEFI code. Introducing that
    infrastructure is tracked in fxbug.dev/34478. A testability exception is granted
    until fxbug.dev/34478 is addressed.
