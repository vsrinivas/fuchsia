# GN integration for Zircon

TODO(fxbug.dev/3156): This is a temporary solution to make selected modules from the
separate Zircon GN build available to the Fuchsia GN build before the two builds
are unified.

This directory hosts generated GN files for Zircon. These files are created
automatically as part of the build by `//build/zircon/create_gn_rules.py` and
should never be manually edited.

In order to expose a Zircon library() target to GN, set the `sdk` parameter:
 - `shared`: the module is exposed as a precompiled shared library;
 - `static`: the module is exposed as a precompiled static library;
 - `source`: the module's sources are published;
It's also mandatory to set the `sdk_headers` parameter.
See library() in //zircon/public/gn/BUILDCONFIG.gn for more details.

host_tool(), banjo_library(), and fidl_library() targets in Zircon are always
exposed in //zircon/public/tool, //zircon/public/banjo, and //zircon/public/fidl
respectively.

Libraries that expose a C++ *interface* (C++ classes, functions, etc in public
headers) *must* be published as `source`, since there is no safe way to support
a binary interface (ABI) in C++.

Libraries that expose a C interface (they may be *implemented* in C++) should be exposed as
`static` or `shared` precompiled libraries.

Small helper libraries are usually exported as `static`.  Larger libraries, especially ones that
we'd expect to stabilize and be used by a wide variety of packages, generally should be shared, for
better de-duplication during packaging and installation.
