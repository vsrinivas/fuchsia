# //third_party/rust_crates/compat

This directory contains additional files for achieving compatibility between our build and the
vendored sources. In many cases it's not needed to fork the source itself but it is to commit some
generated files to git, or write our own GN targets because of a limitation in tooling.
