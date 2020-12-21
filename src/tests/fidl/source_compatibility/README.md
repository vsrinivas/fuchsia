# FIDL Cross-Petal Change Tests

These are tests to validate source-compatibility through various changes to FIDL
libraries. They do not test binary-compatibility. Tests for compiled languages
are only meant to be built, not executed, since source-compatibility issues show
up at compile time.

This directory consists of:

* Tools to generate source compatibility tests. The usage of these tools is
  explained in the [Writing tests](#writing-tests) section.
  * A command line tool, located under `gen/`.
  * A gn template for building source compatibility tests, defined in
    `fidl_source_compatibility.gni`.
* The source compatibility tests themselves. Each test is located in a separate
  directory, which ideally follows the `[parent]-[target]-[change]` format
  used in the FIDL ABI/API compatibility guide. These tests are generated using
  the tool above, and also each contain a generated README.md file describing
  the transition being tested.

## Writing tests (#writing-tests)

A test is declared by defining a test JSON configuration and adding a
`source_compatibility_test` target to the build (for examples, see the
`test.json` files within each test directory). This is done by running the
source compatibility gen tool.

The rest of the commands in this README assume
that this tool is aliased, e.g. by adding the following to your rc file:

```
alias scompat=$FUCHSIA_DIR/src/tests/fidl/source_compatibility/gen/main.py
```

A high level overview of the flow for creating a test using the tools is
as follows:

* Run the test scaffolding tool, giving it the name of the directory it will
  be written to, e.g. `scompat generate_test foo-rename-bar`.
* The first step ("Step 0") of the tool will ask you to define the test title
  as well as the initial states of the FIDL library and for each binding. It
  will ask you for the filename, write a stub file, then pause to let you fill
  out the contents of the file.
* At this step (and after each subsequent step), the tool will output a
  `test.json` file containing the current state of the test, and a `BUILD.gn`
  file declaring a target for the test.
    * You should add the target defined in the build file to the root source
      compatibility test group, so that you can build the test at each step, as
      you write it.
    * It can also be helpful to keep the `test.json` file open as you use the
      tool to verify the changes it makes. You can correct any typos or other
      mistakes that were provided to the tool by editing the JSON file, then
      running `scompat regen`.
* The tool will then alternate between asking you to define the next FIDL change
  and the next bindings change. It will prefill the source files for the next
  step with the contents of the file from the previous step.
* Since the tool output (e.g. `test.json`) is updated at each step, you can
  simply quit the tool when the transition is complete. It can also be helpful
  to build the test between adding steps to ensure that they are correct. Note
  however that the tool does not save the state until you complete the current
  step.

### Modifying existing tests:

If you call the tool on an existing test (i.e. a directory containing a
`test.json` file), it will resume from where you last left off. In other words,
the tool will only append new steps to the test, and cannot insert or
otherwise edit existing steps. You must make edits manually by modifying the
source files and `test.json` file yourself, then run `scompat regen` to
regenerate the auxiliary files that are based on the `test.json`, such as the
README and GN sidecar file.

The test JSON structure is defined in `gen/types_.py`, which contains a number
of classes which correspond directly to the test JSON.

### Debugging tests:

When a test fails to compile, the failure output will contain a path to a place
somewhere in your out directory (what the path represents depends on the
binding, e.g. for C++ the path is the path to the `.o` file, whereas for Dart
it's the path that the source file is copied to before building), that will tell
you exactly which test/FIDL file/source file combination the error is coming
from. For example, if you see the directory

  protocoleventremove_dart_step_03_after_step_02_during_dart_package

somewhere in the path, you can deduce the following based on the ordering:

* The test `protocoleventremove`.
* The language is `dart`.
* The FIDL step being used is `step_03_after`.
* The source file being used is `step_02_during`.

From this, you can deduce that the files in question are
`protocol-event-remove/fidl/step_03_after.test.fidl` and
`protocol-event-remove/fidl/step_02_during.dart`, and use that to debug the
compile error.

## Transition terminology

We sometimes give transitions that follow a specific pattern a name to make it
easier to refer to these kinds of transitions when discussing them.

One set of terms we use is "FIDL assisted" and "source assisted": when
transitioning a FIDL library involves an initial state (**before**), an
intermediate state (**during**), and a final state (**after**), depending on the
bindings used and the kind of change made to the FIDL library, the transition is
either [FIDL-assisted](#fidl-assisted) or [source-assisted](#source-assisted).

### FIDL-assisted {#fidl-assisted}

In a FIDL-assisted transition, you change source code while the FIDL library is
held in a transitional state (e.g., using the `Transitional` attribute). For
these transitions, we test four states:

| Time   | 1      | 2      | 3      | 4     |
| ------ | ------ | ------ | ------ | ----- |
| FIDL   | before | during | during | after |
| Source | before | before | after  | after |

### Source-assisted {#source-assisted}

In a source-assisted transition, you change the FIDL library while source code
held is in a transitional state (e.g., using `default:` in switch statements).
This _would_ lead to testing four states:

| Time   | 1      | 2      | 3      | 4     |
| ------ | ------ | ------ | ------ | ----- |
| FIDL   | before | before | after  | after |
| Source | before | during | during | after |

However, certain FIDL changes require a FIDL-assisted transition in some
bindings and a source-assisted transition in others. Suppose we make a change in
FIDL library _L_ requiring a FIDL-assisted transition in bindings _A_ and
source-assisted in bindings _B_. We would take the following steps:

1. Initially, _L_, _A_, and _B_ are **before**.
2. Change _B_ to **during**.
3. Change _L_ to **during**.
4. Change _A_ to **after**.
5. Change _L_ to **after**.
6. Change _B_ to **after**.

All correct ways of interleaving the steps will have _L_ and _B_ both in the
**during** state at some point. Therefore, although a FIDL **during** state is
unnecessary for a pure source-assisted transition, we must include it in tests.
Thus, we actually test 5 states for source-assisted transitions:

| Time   | 1      | 2      | 3      | 4      | 5     |
| ------ | ------ | ------ | ------ | ------ | ----- |
| FIDL   | before | before | during | after  | after |
| Source | before | during | during | during | after |
