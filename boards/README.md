# Boards

The concept of a board comes from a circuit board with various integrated
circuits mounted on it and the usage herein generally refers to a System on
a Chip (SoC) board. This concept also extends to logical (or virtual) boards for
which there is no physical board.

Common boards are based on arm64 or x64 CPUs. For convenience, this directory
contains `arm64.gni` and `x64.gni` which are imported into other `<board>.gni`
files.

Further details can be found at
[Boards and Products](https://fuchsia.dev/fuchsia-src/development/build/build_system/boards_and_products)
