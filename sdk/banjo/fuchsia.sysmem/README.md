# `fuchsia.sysmem` Banjo library

This library is a copy from a [FIDL library](/sdl/fidl/fuchsia.sysmem). Copied
files were edited to remove constant declarations so that they don't conflict
with their FIDL counterparts in contexts where both Banjo and FIDL bindings are
used.

Ultimately the present library will disappear in favor of its FIDL counterpart.
Note that `metadata.banjo` is an original file and will need to migrate to the
FIDL library.
