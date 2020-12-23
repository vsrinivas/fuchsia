# Kernel command-line arguments

The build file in this directory contains targets for various options for kernel
command-line arguments.

These arguments should be passed via the relevant GN arguments:
- `board_bootfs_labels` (from [`//build/board.gni`](build/board.gni)) in a board file;
- `product_bootfs_labels` (from [`//build/product.gni`](build/product.gni)) in a product file;
- `dev_bootfs_labels` (from [`//build/dev.gni`](build/dev.gni)) in an `fx set` and `gn gen` invocation.
