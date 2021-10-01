# Golden tests for the wisdom example

This directory contains the tests that compare the response printed out by
the polyglot wisdom tests with the contents of `golden-output.txt`.

## Building

If these components are not present in your build, they can be added by
appending `//examples/intl/wisdom/tests:tests` to your `fx set` command.
For example:

```bash
$ fx set core.x64 --with //examples/intl/wisdom/tests:tests
$ fx build
```

If you do not already have one running, start a package server so the example
components can be resolved from your device:

```bash
$ fx serve
```

## Running the tests

To run the test components defined here, provide the build target to
`fx test`:

```bash
$ fx test //examples/intl/wisdom/tests
```
