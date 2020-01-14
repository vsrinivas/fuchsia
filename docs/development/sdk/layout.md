# SDK layout

The SDK archive contains the Fuchsia Core SDK, which is a small set of
Fuchsia-specific libraries and tools required to start building and running
programs for Fuchsia.

This SDK differs from traditional SDKs in that it is not readily usable out of
the box.
For example, it does not contain any build system, favor any
toolchain, or provide standard non-Fuchsia libraries such as for crypto or
graphics.
Instead, it provides metadata accurately describing its various
parts, so that this SDK can be post-processed and augmented with all the pieces
necessary for a satisfactory end-to-end development experience.

Most developers who wish to build something for Fuchsia should not need to
deal directly with this particular SDK.
They will instead consume a transformed version of it, for instance within the
development environment and ecosystem supporting a given language runtime.
Maintainers of development environments who wish to add support for Fuchsia are
the main audience for this SDK.
See [Integrating the Core SDK](integrating.md) for a description of how to process this
SDK.

As such, the Core SDK is the representation of the Fuchsia platform developers'
contract with other developers who work with Fuchsia.
While that contract is absolutely necessary, as this SDK contains the very bits
that are unique to Fuchsia, it is not sufficient and will be complemented by
other "contracts".
The Fuchsia Core SDK is mirroring the Fuchsia platform in that respect: highly
composable and extensible, with a clear separation of concerns.


## Structure

From this point on, the root of the SDK archive will be referred to as `//`.

### Metadata

Metadata is present throughout this SDK in the form of JSON files.
Every element in this SDK has its own metadata file: for example, a FIDL library
`//fidl/fuchsia.foobar` has its metadata encoded in
`//fidl/fuchsia.foobar/meta.json`.

Every metadata file follows a JSON schema available under `//meta/schemas`: for
example, a FIDL library's metadata file conforms to
`//meta/schemas/fidl_library.json`.
Schemas act as the documentation for the metadata and may be used to facilitate
the SDK ingestion process. See [understanding metadata](understanding_metadata.md).

### Documentation

General documentation is available under `//docs` in the SDK distribution, or
 online at [fuchsia.dev/fuchsia-src/docs/development/sdk](/docs/development/sdk).
Some individual SDK elements will also provide documentation directly under the
path where they are hosted in the SDK.

### Target prebuilts

Target prebuilts are hosted under `//arch/<architecture>`.
This includes a full-fledged sysroot for each available architecture.

### Source libraries

The SDK contains sources for a large number of FIDL libraries (under
`//fidl`) as well as a few C/C++ libraries (under `//pkg`). See [compiling C/C++](documentation/compilation.md)
for details.

### Host tools

Multiple host-side tools can be found under `//tools`.
This includes tools for building programs, deploying to a device, debugging,
etc...
Some information about how to use these tools can be found under `//docs`.
Specifically:

* [bootserver](documentation/bootserver.md)
* [zxdb](documentation/debugger.md)
* [device-finder](documentation/device_discovery.md)
* [ssh](documentation/ssh.md)
* [logging and symbolizer](documentation/logging.md)
* [package manager](documentation/packages.md)

### Images

`//device` contains metadata describing device configurations matching a given
version of the SDK.
This metadata contains pointers to images that can be paved onto said devices.
See [working with devices](documentation/devices.md) for how to interact with a device
running Fuchsia.
