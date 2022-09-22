# Compatibility Tests for Fuchsia

The Compatibility Tests for Fuchsia (CTF) is a suite of tests designed
to detect compatibility changes between two different versions of the
Fuchsia platform surface.  To learn how it works, and to get started on adding
CTF tests for your area, please see the links below.

## Fuchsia CTF Definition
* [CTF Overview][overview]: Background, motivation and goals for building the
Fuchsia CTF.
* [CTF RFC][rfc15]: Requirements, design and implementation strategy.
* [FAQ][faq]: Frequently asked questions.

## Contributing to the CTF
* [Contributing Guide][contributing]: One-stop shop with everything you need
to know about contributing to the Fuchsia CTF.  Start here!  Below are a few
examples of CTF tests in action.
* Code Examples
  * [Hello World \[c++\]][hello c++]: A barebones example CTF test written in
C++.
  * [Hello World \[rust\]][hello rust]: A barebones example CTF test written
in Rust.
  * [fuchsia.diagnostics][diag]: An example real CTF test running in
production, protecting the fuchsia.diagnostics FIDL from compatibility issues.

## CTF Test Coverage

Note: TODO: Dashboards are currently internal-only

[overview]: /docs/development/testing/ctf/compatibility_testing.md
[rfc15]: /docs/contribute/governance/rfcs/0015_cts.md
[faq]: /docs/development/testing/ctf/faq.md
[contributing]: /docs/development/testing/ctf/contributing_tests.md
[hello c++]: https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/sdk/ctf/examples/hello_world/
[hello rust]: https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/sdk/ctf/examples/rust/
[diag]: https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/sdk/ctf/tests/fidl/fuchsia.diagnostics/
[cts team]: https://bugs.fuchsia.dev/p/fuchsia/issues/entry?template=Fuchsia+Compatibility+Test+Suite+%28CTS%29
