# Fuchsia Flake Policy

This document codifies the best practice for interacting with test flakes on
Fuchsia.

## Background: What is a flaky test?

A **flaky test** is a test that sometimes passes and sometimes fails, when run
using the exact same revision of the code.

Flaky tests are bad because they:

-   Risk letting real bugs slip past our commit queue (CQ) infrastructure.
-   Devalue otherwise useful tests.
-   Increase the failure rate of CQ, thereby increasing latency for modifying code.

This document is specific to *test flakes*, not infrastructure flakes.

## Requirements: Goals for flaky tests

1.  **Flakes should be removed from the critical path of CQ as quickly as
    possible**.
2.  Since flakes present themselves as a failing test, **flakes should not be
    ignored** once taken off of CQ. They represent a real problem that should be
    fixed.
3.  Tests may flake at any time, and as a consequence, the observer of these
    bugs may not necessarily be the person best equipped to fix it. **The
    process for reporting bugs must be fast, easy, and decoupled from diagnosis
    and patching.**

## Policy

The following provides the expected & recommended lifetime of a flake:

0.  A test flakes in CI or CQ.
1.  Identify: The test is *automatically* identified as a flake.
2.  Track: An issue is *automatically* filed for the identified flake under the Flake component.
3.  Remove: The test is removed from CQ immediately.
4.  Fix: The offending test is fixed offline and re-enabled.

#### Identify

A flake fetching tool is currently in use to identify the vast majority of flakes.

The tool looks for test failures in CQ where the same test succeeded when retried on the same
patch set.

(Googlers-Only) To see the source code for this tool, visit
[http://go/fuchsia-flake-tool](http://go/fuchsia-flake-tool).

#### Track

(Googlers-Only) After flakes are identified, tooling should automatically file an issue for the
flake under the Flake component with label FlakeFetcher. These issues are currently being
manually triaged and assigned. If you experience a test flake, please update existing issues
rather than opening new ones.

To see a list of the currently outstanding flakes, visit
[http://go/flakes-fuchsia](http://go/flakes-fuchsia).

#### Remove

One should prioritize, above all else, removing the test from the commit
queue. This can be achieved in the following ways:

-   If the flake has been prompted by a recent patch: Submitting a revert of a
    patch which triggers this flake.
-   Submitting a change to mark the test as flaky. You can do this by adding
    "flaky" to the `tags` field in the
    [test environment](../development/testing/environments.md). This will remove
    it from the builders that run in the commit queue, and onto special flaky
    builders that continue to run the test in CI. Be sure to note the bug in a
    comment in the BUILD.gn file.
    [Example change](https://fuchsia-review.googlesource.com/c/topaz/+/296629/3/bin/flutter_screencap_test/BUILD.gn).


The above mechanisms are recommended because they remove the flaky test and
prevent the commit queue from becoming unreliable. The first option (reverting code)
is preferred, but it is not as easy as the second option (disabling test), which
reduces test coverage. Importantly, neither of these options prevent diagnosis
and fixing of the flake, but they allow it to be processed offline.

It is **not** recommended to attempt to fix the test without first
removing it from CQ. This causes CQ to be unreliable for all other
contributors, which allows additional flakes to compound in the codebase.

#### Fix

At this point, one can take the filed issue, locally re-enable the test, and work on
reproducing the failure. This will enable them to find the root cause, and fix the
issue. Once the issue has been fixed, the bug can be closed, and the test can be
re-enabled. If any reverted patches need to re-land, they can re-land safely.

## Improvements and Tooling

Ongoing efforts to improve tooling surrounding flakes are actively underway.

These include:

-   Automatically assigning issues for resolving flakes, based on information present in OWNERs
    files. Tracked by 10435.
-   "Deflaking" infrastructure, to re-run tests in high volume before they are
    committed. Tracked by 10011.

As improvements are made, this document will be updated with the latest policy.

