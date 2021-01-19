# Bringup test modernization

<<../_stub_banner.md>>

## Goal & motivation

The Bringup product specification is the most minimal viable target for
development. It's commonly used for very low-level engineering work, such as
kernel development and board-specific drivers and configuration (also known as
board bringup, hence the name). Many fundamental engineering workflows and `fx`
commands don't work in Bringup.

The next product configuration is called Core. Core adds support for additional
engineering workflows, including those used by the testing infrastructure. It is
therefore more convenient to run tests on Core when possible, resorting to
running tests on Bringup when unavoidable.

We would like to move tests from Bringup to Core as much as possible, in order
to improve the developer experience by allowing for faster iteration cycles and
easier troubleshooting. Tests that absolutely need to run in Bringup should have
a stated reason for this.

## Technical background

The scope of Bringup tests is defined in
[`//bundles/bringup/BUILD.gn`](/bundles/bringup/BUILD.gn)
under the group `"tests"`.

## How to help

### Picking a task

Pick any test target from
[`//bundles/bringup/BUILD.gn`](/bundles/bringup/BUILD.gn).

### Doing a task

Move that test to the Core configuration. Attempt to run the test locally or on
CQ, and troubleshoot as needed.

Most tests that are moved from Bringup to Core are expected to fail at first,
but the root cause is typically trivial configuration and doesn't require
changing the substance of the test.

As you run into common failure modes and solutions, please consider documenting
them here for reference.

### Completing a task

Find reviewers by OWNERS and merge your change.

## Examples

*   463175: [core] Move bootfs_test() to core. |
    https://fuchsia-review.googlesource.com/c/fuchsia/+/463175
*   434303: [unification] Package and move several bringup tests |
    https://fuchsia-review.googlesource.com/c/fuchsia/+/434303
*   435589: [testing] Package and move several bringup tests |
    https://fuchsia-review.googlesource.com/c/fuchsia/+/435589

## Sponsors

Reach out for questions or for status updates:

*   gboone@google.com.com
