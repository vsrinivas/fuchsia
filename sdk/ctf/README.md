# Compatibility Test Suite

This directory contains the Fuchsia Compatibility Test Suite (CTS).  The Fuchsia
CTS is a set of tests designed to provide coverage for Application Programming
Interface (API) and Application Binary Interface (ABI) elements (e.g., headers,
FIDL files) made available to developers via a Fuchsia SDK. It was originally
proposed as [RFC
0015](https://fuchsia.dev/fuchsia-src/contribute/governance/rfcs/0015_cts).

To learn more, see our documentation on
[fuchsia.dev](https://fuchsia.dev/fuchsia-src/development/testing/cts/overview).
See [//sdk/ctf/examples] for some examples, or peruse the complete set
of tests under [//sdk/ctf/tests].


## //sdk/ctf Directory structure

A brief rundown of the //sdk/ctf directory layout:

* **build/**: CTS-specific build rules and scripts.
* **canary/**: Files that allow us to modify the CTS tests running in the
canary release.
* **data/**: Location of static metadata that will be packaged alongside
the tests in each CTS release.
* **examples/**: Example test cases running in the CTS.
* **plasa/**: Build rules, scripts and config files for generating the
Platform Surface Area (PlaSA) definition files.
* **release/**: Files that allow us to modify the CTS tests running in
main CTS releases. Also includes scripts for validating any given CTS release.
* **tests/**: Location of the tests in this Compatibility Test Suite.
* **util/**: Binaries and utilities needed to run individual end-to-end CTS
tests.


## Contributing tests

See the [CTS Contributing Guide].

## Running example tests

To run example tests:

```
fx set core.qemu-64 --with //sdk/ctf/tests/examples/
fx test //sdk/ctf/tests/examples/
```

#### Language

##### Target-side tests

Tests for particular headers must be written in a language that supports that
header.  C headers currently target C11 and C++11 and above.  C++ headers
currently target C++14 and above.  This policy may change as we build a larger
test corpus and decide how to enforce C++14 compatibility.

All tests that target API should be written in a language that is supported for
end developers per the [Fuchsia language policy].  The CTS currently only
provides direct support for C++ for tests that target API.

Tests that are only intended to exercise ABI may be written in any language
supported for use in the Fuchsia Platform Source Tree, including Rust.  This
may, for example, include tests that check startup handles or the format of the
VDSO.

For the most part, tests that exercise FIDL definitions and other APIs shipped
with SDKs are API tests that should be written in C++.  However, if this places
an undue burden on test authorship (e.g., there are large frameworks needed for
the test or a substantial body of appropriate existing tests that are written in
Rust), we can make exceptions. Note the following:

* If you do not use C++, your API will not undergo build time compatibility
  testing.  This places a burden on anyone trying to deploy an SDK, because
  changes to your API are more likely to break compilation of petal code.

* If there are large test frameworks or libraries that you need to use to write
  tests for public APIs, and they are only written in a language that is not
  officially supported for SDK users (see the [Fuchsia language policy] doc),
  that means that there is a form of testing that you need for exercising
  developer use cases that you are not providing to developers.  Consider
  whether those test frameworks should be provided in a language that is
  supported for end-developers.

* There will be a high bar for language exceptions for teams that wish to use
  other languages only because of unfamiliarity with C++.  We encourage API
  developers to understand how end-developers use their APIs.

For information about exceptions to the C++-only policy, file a bug in the [CTS
bug component].

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
include a third party dependency, file a bug in the [CTS bug component] to reach
out to the team to discuss it.

Code that runs on the host does not have this restriction.


[CTS Contributing Guide]: /docs/development/testing/cts/contributing_tests.md
[Fuchsia language policy]: https://fuchsia.dev/fuchsia-src/contribute/governance/policy/programming_languages
[CTS bug component]: https://bugs.fuchsia.dev/p/fuchsia/templates/detail?saved=1&template=Fuchsia%20Compatibility%20Test%20Suite%20%28CTS%29&ts=1627669234
[//sdk/ctf/examples]: https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/sdk/ctf/examples/
[//sdk/ctf/tests]: https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/sdk/ctf/tests/
