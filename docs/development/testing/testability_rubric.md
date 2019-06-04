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
    agree that it’s tested, and consider applying Testability-1 along with a
    note for what’s missing if it’s not.
*   Focus on whether the change is tested, not necessarily on what the change
    actually does. For instance you may apply Testability+1 if the change is
    well tested and at the same time Code-Review-1 if you would not like to see
    the change merged for other reasons.
*   Apply the standard (this doc) consistently.
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
    should be if you rely on it working as intended (for instance in fx
    debug-report or Feedback).
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

## Temporary testability exemptions

The following are currently exempt from Testability, while ongoing work aims to
change that.

*   **Engprod scripts (e.g. `fx` commands) and associated configuration files**
    have an exemption from testability in the near term. We would like to test
    these scripts in the long term, but we’ve decided not to block engineering
    productivity improvements on creating such tests.
*   **GN templates** are not easily testable. We are working on a test framework
    for GN templates. Until then, it's permitted for build template changes to
    be manually tested only.
