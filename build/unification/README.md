# AllN

> TODO(fxbug.dev/3367): delete this directory.

This directory contains transient build infrastructure used for the AllN effort
which aims at producing a single GN/ninja build for the entire Fuchsia system.

The `zn_build` directory hosts build templates that are sneakily inserted into
`//zircon/*/BUILD.gn` files so that they integrate with the Fuchsia GN build.

## Zircon library mappings

The `zircon_library_mappings.json` file configures some forwarding targets
installed under `//zircon/public/lib` to assist with the migration of libraries.

Entries in this file should be added to the top-level array with the following
format:
```
{
  "name": "foobarblah",
  "label": "//zircon/system/ulib/halbraboof",
  "sdk": true
}
```

All attributes are required:
- `name` is the name of the forwarding target under `//zircon/public/lib`;
- `label` is the GN label of the library the target should forward to;
- `sdk` controls whether an additional forwarding target should be set up for
  the library.

To use this data in GN, import `zircon_library_mappings.gni`. The source of
truth is a JSON file so that it may easily be read by the Python script
generating the mappings (`//build/zircon/populate_zircon_public.py`).
