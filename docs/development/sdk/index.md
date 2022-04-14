# Fuchsia SDK

This section is for developing Fuchsia software or Fuchsia products
using the Fuchsia SDK.

Important: The Fuchsia SDK is in active development. At the moment, Fuchsia
does not support general public usage of the Fuchsia SDK. The APIs in the SDK
are subject to change without notice.

## Developing software with the Fuchsia SDK

The Fuchsia SDK is a set of build rules, API headers, code libraries, and
[host tools][host-tools] put together to enable a Fuchsia software development
environment. With the Fuchsia SDK, developers can create, build, run, test, and debug
[Fuchsia components][fuchsia-components] and drivers (that is, Fuchsia software)
without needing to set up a [Fuchsia source checkout][fuchsia-platform]
(`fuchsia.git`) on the host machine.

## Developing a product with the Fuchsia SDK

Fuchsia products in general include software that runs on top of the
Fuchsia platform and operating system. Using the Fuchsia SDK, developers can
combine the Fuchsia operating system with a customized blend of software to
assemble and produce a new Fuchsia-based product.

## Bazel build system

The Fuchsia SDK supports [Bazel][bazel]{:.external} as  an
out-of-the-box solution for building and testing software. However, Bazel is not
a strict requirement. The Fuchsia SDK is designed to be integrated with
most build systems to meet the needs of a diverse development ecosystem.


<!-- Reference links -->

[host-tools]: https://fuchsia.dev/reference/tools/sdk/ffx
[fuchsia-components]: /docs/concepts/components/v2
[fuchsia-platform]: /docs/development
[bazel]: https://bazel.build/docs
