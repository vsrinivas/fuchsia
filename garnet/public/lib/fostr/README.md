# fostr: Output Formatting

fostr is an ostream formatting library that provides the following:

* support for indented formatting (indent.h)
* hex dumps (hex_dump.h)
* formatting for public/lib/fidl types (fidl_types.h)
* formatting for zircon zx:: types (zx_types.h)
* generated formatting for FIDL types

fostr is not part of the Fuchsia SDK.

## FIDL type formatting

Currently, only structs, unions and enums can be formatted. Tables will be
supported, and support for formatting serialized messages is also intended.

To use an existing formatter for FIDL types (say, for fuchsia.foo), add the
dependency to your BUILD.gn file, which will look like this:

```
//garnet/public/lib/fostr/fidl/fuchsia.foo
```

Includes look like this:

```
#include "garnet/public/lib/fostr/fidl/fuchsia/foo/formatting.h"
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
