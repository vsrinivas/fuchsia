The actual source and header files come from the verbatim upstream checkout
found at //third_party/zstd.  This directory just provides the BUILD.gn file
to stitch it into the Zircon build, and include/zstd/ with wrapper headers
to redirect <zstd/zstd.h> and <zstd/zstd_seekable.h> to the real source
location.

TODO(fxbug.dev/3156): After build unification, this might move to
.../secondary/third_party/zstd so the //third_party/zstd path can be used
in GN deps while still tracking the upstream repo unmodified.
