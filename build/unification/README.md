# AllN

> TODO(3367): delete this directory.

This directory contains transient build infrastructure used for the AllN effort
which aims at producing a single GN/ninja build for the entire Fuchsia system.

The `zn_build` directory hosts build templates that are sneakily inserted into
`//zircon/*/BUILD.gn` files so that they integrate with the Fuchsia GN build.
