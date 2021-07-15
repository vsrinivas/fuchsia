# //third_party/rust_crates/empty

This directory contains "null forks" of crates. Most of these crates are needed within the build
graph because of an external transitive dependency which isn't actually used on the targets built
by Fuchsia. Some of these crates have licensing or policy conflicts, while others would
create an unnecessary review burden.
