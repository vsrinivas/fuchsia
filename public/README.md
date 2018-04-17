# GN integration for Zircon

This directory hosts generated GN files for Zircon. These files are created
automatically as part of the build by `//build/zircon/create_gn_rules.py` and
should never be manually edited.

In order to expose a Zircon module to GN, set the `MODULE_PACKAGE` attribute in
its `rules.mk` build file. The possible values are:
 - `bin`: the module is exposed as a host application;
 - `src`: the module's sources are published;
 - `shared`: the module is exposed as a precompiled shared library;
 - `static`: the module is exposed as a precompiled static library;
Note that this currently only applies to `ulib` and `hostapp` modules.

Libraries that expose a C++ *interface* (C++ classes, functions, etc in public headers) *must* be
published as `src`, since there is no safe way to support a binary interface (ABI) in C++.

Libraries that expose a C interface (they may be *implemented* in C++) should be exposed as
`static` or `shared` precompiled libraries.

Small helper libraries are usually exported as `static`.  Larger libraries, especially ones that
we'd expect to stabilize and be used by a wide variety of packages, generally should be shared, for
better de-duplication during packaging and installation.
