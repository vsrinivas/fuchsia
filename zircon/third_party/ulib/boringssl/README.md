The actual source and header files come from the verbatim upstream checkout
found at //third_party/boringssl.  This directory just provides the BUILD.gn
file to stitch it into the Zircon build, and include/openssl/ with wrapper
headers to redirect each header to the real source location.
