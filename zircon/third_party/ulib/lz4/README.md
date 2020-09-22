The actual source and header files come from the verbatim upstream checkout
found at //third_party/lz4.  This directory just provides the BUILD.gn file
to stitch it into the Zircon build, and include/lz4/ with wrapper headers
to redirect <lz4/lz4frame.h> to the real source location.

TODO(fxbug.dev/3156): After build unification, this might move to
.../secondary/third_party/lz4 so the //third_party/lz4 path can be used in
GN deps while still tracking the upstream repo unmodified.
