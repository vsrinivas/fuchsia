fx codesize
=================================================

`fx codesize` is a binary size analytics tool for ELF executables and libraries,
sifting through symbols and debug information to produce statistics about their
size and categorizations, helping to make informed decisions on which pieces of
code should have their size kept in check, and which code should be the focus
for binary size optimization.

## Usage Tips

Before running `fx codesize`, one should perform a full build (fx build),
which would update the system images that codesize uses. Every time another
full build happens, codesize would detect this by default, and re-analyze
all the binaries.

`fx codesize --help` has more detailed help information and details on each
query.

## Development Tips

When using VSCode, install the Dart plugin, and add `//tools/codesize` as a
top-level folder in the workspace. IDE features such as autocomplete, Dart
analysis etc. should start working after an initial initialization period.

Add `--with //tools:tests` to the `fx set` line. Afterwards,
use `fx test //tools/codesize:codesize_tests` to run the unit tests.
Additionally, `fx test //tools/codesize:codesize_tests -vo` will print out all
the tests as they are being run.
