# Regenerating fidlc Golden Files

Ensure `fidlc` is built, for instance

    fx build host_x64/fidlc

Then run the `regen.sh` script:

    fx exec zircon/tools/fidl/testdata/regen.sh

This script runs fidlc on all of the inputs in the `typestest/` directory, and
outputs them into the `goldens/` directory. The `json_generator_tests` are a
fidlc unit test that will ensure that the output inside `goldens/` matches the
current state of the fidl compiler.

Each "input" in `typestest/` corresponds to a single json output file in
`goldens/`, and can take one of two possible forms:

  - A single fidl file, `foo.fidl` which gets converted to `foo.json`
  - A directory `foo` which gets converted to `foo.json`. This directory must
    contain:
    - One or more fidl files.
    - An `order.txt` file that describes the dependency ordering of the files in
      this directory. For example, for a library `foo` that consists of two fidl
      files `a.fidl` and `b.fidl` where `a.fidl` depends on `b.fidl`,
      `order.txt` would consist of two lines, the first being `b.fidl` and the
      second being `a.fidl`.

Currently `json_generator_tests.cc` only supports a "linked list" shaped
dependency tree, where each fidl file depends on only the fidl file in the line
above it (except for the first fidl file which has no dependencies)
