Naming C/C++ objects
====================

## Include paths

The following guidelines apply to libraries which are meant to be used
extensively, e.g. in an upper layer of the Fuchsia codebase or via an SDK.

#### For system headers

```
<zircon/foo/bar.h>
```

###### Rationale

These headers describe kernel interfaces (syscalls, related structs and
defines), shared definitions and data structures between kernel and userspace
(and bootloader), that are often useful to higher layers as well.

###### Notes

- Headers may be installed straight under `zircon/`.

###### Examples

- `zircon/process.h`
- `zircon/syscalls/hypervisor.h`


#### For global headers

```
<fuchsia/foo/bar.h>
```

###### Rationale

These are libraries very specific to Fuchsia but not particularly to the kernel.

###### Notes

- We will possibly install FIDL-generated code for Fuchsia APIs in that very
  namespace, as well as C/C++ wrapper libraries around these APIs.
- Headers may be installed straight under `fuchsia/`.
- There are no known uses of this format as of this writing.

###### Examples

- `fuchsia/fdio/fdio.h`
- `fuchsia/pixelformat.h`


#### For other headers

```
<lib/foo/bar.h>
```

###### Rationale

Some libraries in that space are not necessarily Fuchsia-specific, so we are
using a rather bland namespace that will likely not cause any collisions in the
outside world.

###### Notes

- Headers may not be placed straight under `lib/`. Subdirectories (`lib/foo/`)
  are mandatory.

###### Examples

- `lib/app/cpp/application_context.h`
- `lib/fbl/array.h`
