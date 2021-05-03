# Base Package

This Rust crate constructs the Base Package, which is a Fuchsia package
containing files necessary to bootstrap `pkgfs`. Historically, it has also been
called the System Image.

Currently, three types of files exist in the Base Package:

1. Lists of package mappings between their name and the merkle of their
meta.far. `pkgfs` uses these mappings to load the packages from `blobfs`.
1. Product configuration for `pkgfs` (e.g. allowlists).
1. Drivers and their dependency libs.
