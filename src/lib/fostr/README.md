# fostr: Output Formatting

fostr is an ostream formatting library that provides the following:

* support for indented formatting (indent.h)
* hex dumps (hex_dump.h)
* formatting for public/lib/fidl types (fidl_types.h)
* formatting for zircon zx:: types (zx_types.h)
* generated formatting for FIDL types

fostr is not part of the Fuchsia SDK.

## FIDL type formatting

Currently, structs, tables, unions, arrays, vectors, and enums can be formatted.
Support for formatting serialized messages is also intended.

To use an existing formatter for FIDL types (say, for fuchsia.foo), add the
dependency to your BUILD.gn file, which will look like this:

```
//src/lib/fostr/fidl/fuchsia.foo
```

Includes look like this:

```
#include "src/lib/fostr/fidl/fuchsia/foo/formatting.h"
```

Adding a new FIDL library is pretty simple: just add a new fostr/fidl/xxx
subdirectory with a BUILD.gn (see existing files for an example). The target
must depend on the fostr/fidl targets for the libraries on which
xxx depends. This means you may have to add formatting support for
libraries that your FIDL library depends on.

If you have hand-rolled formatters for a library, they
can be accommodated. Look for directories containing a file called
'amendments.json' for examples of this.

If you have formatters for all the types in your library, you can just provide
a formatting.h and skip the amendments business. There are currently no
examples of this.

### FIDL containers properly print `int8` and `uint8` as integers

All FIDL containers (structs, tables, unions, arrays, and vectors) print `int8`
and `uint8` FIDL primitives as integers. Structs, tables and unions interpret
these as 32-bit types for printing. Arrays and vectors use hex formatting.

Note that if you choose to print an 8-bit data type directly, the C++
STL interprets these as `char` types, so you'll have to cast it yourself:

```
uint8_t my_fidl_data = get_some_uint8();
os << static_cast<uint32_t>(my_fidl_data);
```

## Implementation notes

### Code-generation templates

The corresponding code-generation templates are located in the sibling directory:

```sdk/lib/fostr/build```
