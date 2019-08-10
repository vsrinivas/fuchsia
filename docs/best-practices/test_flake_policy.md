# Fuchsia Flake Policy

This document codifies the best practice for interacting with test flakes on
Fuchsia.

## Background: What is a flaky test?

A **flaky test** is a test that sometimes passes and sometimes fails, when run
using the exact same revision of the code.

Flaky tests are bad because they:

-   Risk letting real bugs slip past our commit queue (CQ) infrastructure.
-   Devalue otherwise useful tests.
-   Increase the failure rate of CQ, increasing latency for modifying code.

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

The process for identifying and fixing flake is intentionally decoupled from
between two parties:

**Observer**: An individual who has witnessed flake on bots.

**Resolver**: An individual who has the ability to remove flake from the bots.

This separation satisfies requirement (3): if someone contributes to Fuchsia, it
is their responsibility to act as an observer for the entire codebase, and their
responsibility to act as a resolver for code they touch.

We recommend the following four-step process for dealing with flakes:

1.  Observer: Identify the flake.
2.  Observer: File a bug under the FLK project.
3.  Resolver: Remove the offending test from CQ immediately.
4.  Resolver: Fix the offending test offline, re-enable the test.

#### Observer: Identify

Flake can appear in many locations: CQ dry-runs, an actual CQ run, or in the
roller into global integration. In any of these cases, flake can be identified
as a test that sometimes passes and sometimes fails, with the same revision of
the codebase. When a test is identified this way, a log should be captured,
providing context and revealing which subtest failed.

#### Observer: Bug

(Googlers-Only) File a bug under go/test-flake: Link to the failing bot, and
include the name of the failing test.

#### Resolver: Remove from CQ

A resolver should prioritize, above all else, removing the test from the commit
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

The above mechanisms are recommended because they remove the faulty test, and
prevent the commit queue becoming unreliable. The first option (reverting code)
is preferred, but it is not as easy as the second option (disabling test), which
reduces test coverage. Importantly, neither of these options prevent diagnosis
and fixing of the flake, but they allow it to be processed offline.

It is **not** recommended to attempt to fix the test without first
removing it from CQ. This causes the CQ to be unreliable for all other
contributors, which allows additional flakes to compound in the codebase.

#### Resolver: Fix Offline

At this point, the resolver can take the bug filed by the observer, locally
re-enable the test, and work on reproducing the failure. This will enable them
to find the root cause, and fix the issue. Once the issue has been fixed, the
bug can be closed, and the test can be re-enabled. If any reverted patches need
to re-land, they can re-land safely.

## Improvements and Tooling

Ongoing efforts to improve tooling surrounding flake are currently in progress.

These include:

-   Automatically filing flake bugs, removing the "Observer" role from the path
    to identify flakes. Tracked by IN-1118.
-   Automatically assigning flake bugs, based information present in OWNERs
    files. Tracked by IN-1670.
-   "Deflaking" infrastructure, to re-run tests in high volume before they are
    committed. Tracked by IN-1231.

When these improvements are available, this document will update to include the
adjusted policy.
