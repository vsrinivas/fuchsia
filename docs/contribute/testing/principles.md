# Testing principles

Testing is central to the
[continuous integration][continuous-integration]{:.external} processes that
Fuchsia contributors practice to sustain quality and velocity. Good tests become
an asset to the team, while bad tests can turn into a liability.

This document reviews topics related to testing on Fuchsia, and provides
references to additional resources. This document assumes that you are
familiarized with general software testing concepts.

Fuchsia platform testing should serve the goals of the project and align with
[Fuchsia's architectural principles][principles]:

- **[Secure][principles-secure]**: Tests are subject to the same security,
  isolation, and hermeticity mechanisms that apply to production software. Tests
  leverage those same mechanisms for their benefit. The security properties of
  the system are testable.
- **[Updatable][principles-updatable]**: It's important to test the stable
  interfaces between [components][glossary.component] and other parts of the
  stable [Fuchsia System Interface][fsi]. If components under tests change or
  are [entirely replaced][netstack3-roadmap], tests that only exercise
  interfaces between components should continue to work. Tests that are outside
  of Fuchsia tree should not assume implementation details of platform
  components.
- **[Inclusive][principles-inclusive]**: Testing frameworks and testing guides
  should either not assume that the developer is using a particular runtime,
  programming language, or hardware, or alternatively demonstrate that
  developers can make their own choices, such as by providing support in
  multiple distinct languages and making it easy to bring up new language
  support. Tests for public and open source Fuchsia code should themselves be
  public and open source.
- **[Pragmatic][principles-pragmatic]**: When the principles presented above are
  in conflict with each other or with important near-term goals, it’s ok to make
  pragmatic choices. It is usually not important to meet an arbitrary bar, for
  instance a fixed percentage of test coverage, or a particular mix of
  unit, integration, and system tests.

## How is testing different on Fuchsia?

### Operating systems are complex programs

Every domain of software development and testing brings unique challenges. There
are special problems and solutions to testing an operating system, as there are
for testing a mobile application or server software or a spacecraft.

For instance, tests for the [Zircon kernel][glossary.zircon] run in a special
runtime environment that makes as few assumptions as possible that the kernel is
functional, and can detect mistakes in low-level kernel code. Contrast this with
typical application testing, where the tests run on some operating system that
is assumed to be working.

### Isolation and hermeticity

The [Component Framework][cf] promotes Fuchsia’s security goals by running each
component in a sandbox environment that is strictly-defined in a
[component manifest][cf-manifests]. It then promotes Fuchsia’s updatability
goals by only allowing components to interconnect using those
[component capabilities][cf-capabilities] that are typed as updatable contracts.

These same mechanisms of isolation and hermeticity can also be used by tests as
a form of [dependency injection][wikipedia-dependency-injection]{:.external}.
For instance a component under test can be provided a test double for a
capability that it depends on by a test harness, making
[contract testing][contract-test]{:.external} easier.

### Multiple repositories

Fuchsia is a large project with many external dependencies, and builds against
hundreds of other source code repositories. Multi-repository development
introduces unique challenges to testing. A contributor working on the WebRTC
project published a [blog post][multi-repo-dev]{:.external} detailing many of
the problems and solutions that are also encountered when developing Fuchsia.

## Further reading

* [Testing scope][test-scope]
* [Testing best practices][best-practices]

[fsi]: /docs/concepts/system/abi/system.md
[netstack3-roadmap]: /docs/contribute/roadmap/2021/netstack3.md
[test-scope]: /docs/contribute/testing/scope.md
[best-practices]: /docs/contribute/testing/best-practices.md
[continuous-integration]: https://martinfowler.com/articles/continuousIntegration.html
[principles]: /docs/concepts/index.md
[principles-inclusive]: /docs/concepts/principles/inclusive.md
[principles-pragmatic]: /docs/concepts/principles/pragmatic.md
[principles-secure]: /docs/concepts/principles/secure.md
[principles-updatable]: /docs/concepts/principles/updatable.md
[glossary.component]: /docs/glossary/README.md#component
[glossary.zircon]: /docs/glossary/README.md#zircon
[cf]: /docs/concepts/components/v2/README.md
[cf-capabilities]: /docs/concepts/components/v2/capabilities/README.md
[cf-manifests]: /docs/concepts/components/v2/component_manifests.md
[wikipedia-dependency-injection]: https://en.m.wikipedia.org/wiki/Dependency_injection
[contract-test]: https://martinfowler.com/bliki/ContractTest.html
[multi-repo-dev]: https://testing.googleblog.com/2015/05/multi-repository-development.html
