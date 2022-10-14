# batchtester

`batchtester` is a tool that runs a batch of test executables in sequence,
performing setup of a Fuchsia target device before running the tests if
necessary.

Its primary customer is out-of-tree projects using the Bazel SDK, but it is
intended to be an eventual replacement for `testrunner`
(`//tools/testing/testrunner`).

`batchtester` is intended to amortize the cost of device setup and run on
Fuchsia-managed testing machines, neither of which is possible out-of-the-box
with `bazel test`.

Design doc: <http://goto.corp.google.com/oot-infra-testing>
