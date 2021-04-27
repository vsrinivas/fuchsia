# Compatibility Test Suite

This directory contains the Fuchsia Compatibility Test Suite (CTS).  The Fuchsia
CTS is a set of tests designed to provide coverage for Application Programming
Interface (API) and Application Binary Interface (ABI) elements (e.g., headers,
FIDL files) made available to developers via a Fuchsia SDK. It was originally
proposed as (RFC
0015)[https://fuchsia.dev/fuchsia-src/contribute/governance/rfcs/0015_cts].

## Background, Motivation, and Goals

The CTS exists to determine whether a build of the Fuchsia platform, running on
a particular device, correctly implements (or *is compatible with*) the API and
ABI exposed by a particular Fuchsia SDK.  To put it another way, it demonstrates
that the build correctly implements Fuchsia.

If a system running Fuchsia passes the CTS tests for a particular ABI revision,
then its developers can have confidence that components built for that revision
will work on the system, and that the system is backwards compatible with that
revision.

Each release of the Fuchsia platform is bundled with a set of Software
Development Kits (SDKs), which contain tools, libraries, and headers that allow
developers to target Fuchsia's APIs and ABIs.  Each SDK will be paired with a
set of CTS tests that exercise the API and ABI it exposes.  The tests will be
available in both source and binary form.

CTS tests are not comprehensive.  They cannot guarantee that any component that
is built against one SDK will run against any particular build of Fuchsia.  CTS
must, therefore, be complemented with product-specific testing.

With that in mind, the CTS can then be used to enforce compatibility in the
following ways:

### For platform developers, including those working in fuchsia.git, system integrators, product developers, and device developers

The CTS binary tests can be run against a device running Fuchsia to ensure that
the build on that device is ABI compatible with the CTS's associated SDK.  This
can provide enforcement of backwards compatibility guarantees: if the CTS from a
given SDK passes, that is a strong indicator (although not a conclusive one)
that programs built against that SDK will continue to work.  It can also provide
confidence that software running on the device that is not exercised by in-tree
tests, such as out-of-tree device drivers, does not change the behavior of the
platform.

As a table:


| Run → Against ↓                       | CTS N  | CTS N + 1   |
|---------------------------------------|--------|-------------|
| SDK / Product Build at version N      | A      | B           |
| SDK / Product Build at version N + 1  | C      | A           |

Where:

A = Someone who wants to make sure a product build correctly implements the ABI
revision they claim it does.

B = Someone who wants to make sure that a product build is forward compatible
with the a newer ABI revision.  Fuchsia org doesn't provide this kind of
guarantee.

C = Someone who wants to make sure that a product build is backwards compatible
with an older ABI revision.  Fuchsia org provides this kind of guarantee to
customers whose code targets older SDKs.

### For SDK vendors

The CTS source tests can be built against an SDK to ensure that the SDK is API
compatible with the CTS's associated SDK.  Additionally, CTS contains a suite of
tests for SDK host tools.  These tests can provide confidence that changes to
the SDK do not break developer code and workflows.  For example, we can build
the CTS for API version N against an SDK that contains support for API version
N+1 to see if the SDK has broken API compatibility.  Currently, the only SDK
vendor supported by the CTS project is the Fuchsia organization itself.

As a table:

| Build → Against ↓                     | CTS N  | CTS N + 1   |
|---------------------------------------|--------|-------------|
| SDK at version N                      | A      | B           |
| SDK at version N + 1                  | C      | A           |

Where:

A = Someone who wants to make sure an SDK correctly implements the API level
they claim it does.  This includes Fuchsia org (testing at tip of tree).

B = Someone who wants to make sure that an SDK is forward compatible with the a
newer API level.  Fuchsia org doesn't provide this kind of guarantee.

C = Someone who wants to make sure that an SDK is backwards compatible with an
older API level.  Fuchsia org provides this kind of guarantee to customers whose
code targets older SDKs.


## Writing tests

This section provides guidelines on how to author CTS tests.

### Directory structure

The structure of the `//sdk/cts` directory mirrors the structure of SDK
artifacts.  Tests should go in the same directory as the interface under test is
found in an SDK.  For example:

  * Tests for host tools should go in `//sdk/cts/tests/tools`
  * Tests for FIDL interfaces should go in the appropriate subdirectory of
      `//sdk/cts/tests/fidl`.  For example, tests for `fuchsia.sysmem` should go
      in `//sdk/cts/tests/fidl/fuchsia.sysmem`.
  * Tests for libraries should go in the appropriate subdirectory of
    `//sdk/cts/tests/pkg`.  For example, tests for `async-loop` should go in
    `//sdk/cts/tests/pkg/async-loop`.

### Build support

CTS tests target API and ABI available through externally-available SDKs.  Build
support ensures that tests only depend on API elements that are available via an
SDK, or whitelisted for use within the CTS.  All build targets must use the
`cts_` rule variants found in `//sdk/cts/build` instead of the standard
fuchsia.git rules (i.e., use `cts_fuchsia_component`, `cts_executable`, and so
on).

The allowlist for non-SDK code can be found in
`//sdk/cts/build/allowed_cts_deps.gni`.  Test authors who believe they need an
additional inclusion should reach out to the OWNERS of this directory.

### Running example tests

To run example tests, append `--with //sdk/cts` to `fx set`
eg, `fx set core.qemu-64 --with //sdk/cts`

Next, run `fx test //sdk/cts/examples/`

### Writing tests

For example tests, see `//sdk/cts/examples`.

#### Language

##### Target-side tests

Tests for particular headers must be written in a language that supports that
header.  C headers currently target C11 and C++11 and above.  C++ headers
currently target C++14 and above.  This policy may change as we build a larger
test corpus and decide how to enforce C++14 compatibility.

All tests that target API must be written in a language that is supported for
end developers per the [Fuchsia language policy].  The CTS currently only
provides direct support for C++ for tests that target API.

In the future, we plan on supporting ABI-only tests that can be written in
different languages (e.g., Rust) and delivered as prebuilts into the CTS.

##### Host-side tests

For end-to-end tests and scripts that run on the host, we support the use of
Dart (and, specifically `sl4f`).

#### Dependencies

Since tests are designed to exercise the SDK APIs and ABIs, dependencies on SDK
elements (tools, APIs) are allowed.

See the section on [build support](#Build-support) for information on including
new dependencies on first-party code.

In order to avoid relying on third party use of the SDK to test the SDK, CTS
tests that run on-device do not rely on third party frameworks that rely on the
SDK to build.  This is why we use `zxtest` instead of `gtest`.  If you want to
include a third party dependency, contact the OWNERS of `//sdk/cts/`.

Code that runs on the host does not have this restriction.

#### Testing Dos and Don'ts

Tests should contain a check for every documented assertion about a particular
API or ABI.  For example, if we have a class `fit::basic_string_view`, and it
has a method `size` that is documented to return the size of the string_view, we
would have a test that creates a string_view, calls the `size` method, and
asserts that the return value is correct.

Tests should reflect best practices about the usage of a given API.  Informally,
if an end developer were to see the test, and copy its usage of the API, the
test author would believe that developer would be using the API correctly. Tests
should, to the extent possible, not depend on undocumented, application-specific
invariants.  In the future, in the case of widespread use of undocumented usages
outside of the Fuchsia tree, we may need to support use cases that do not follow
recommended usages.

Wherever possible, tests should avoid creating test doubles (e.g., mocks and
fakes) for the internal state of the target device.  The intent of the CTS is to
make sure the entire device behaves correctly, not to make sure that a
particular component behaves correctly in isolation.

Tests must take care to leave the system in its original state.  For example, a
test might make a system-wide change to set the foreground and background color
of text to the same color. The colors should be reset at the end of the test.
In the future, we plan to create enforcement mechanisms that allow tests to
declare that they permanently change the state of the device, so that the
framework can do the reset itself.  Until such enforcement mechanisms are in
place, tests should reset the system to their initial state.

## Questions

For questions and clarification on this document, please reach out to the
directory's owners.

[Fuchsia language policy]: https://fuchsia.dev/fuchsia-src/contribute/governance/policy/programming_languages
