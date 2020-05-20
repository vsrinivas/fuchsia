# fidldev

`fidldev` is a FIDL development workflow tool. Its goal is to automate
repetitive processes while working on FIDL code, like running tests based on
changed files, and regenerating golden files. It is also meant to be the
source of truth for FIDL code locations and tests/regen commands.

## Running fidldev:

    tools/fidl/fidldev/fidldev.py --help

This can be aliased for convenienced:

    alias fidldev=$FUCHSIA_DIR/tools/fidl/fidldev/fidldev.py

## Testing fidldev:

    python3 tools/fidl/fidldev/fidldev_test.py -b

The `-b` flag will separate stdout output when printing test results. It can
be removed when using debugging print statements in the test itself.
