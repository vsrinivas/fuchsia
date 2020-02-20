# Hello World Components

This directory contains simple components to show how components are built,
run, and tested in the system.

## Building

If these components are not present in your build, they can be added by
appending `--with //examples` to your `fx set` command. Eg:

```bash
$ fx set core.x64 --with //examples --with //examples:tests --with //src/sys/run_test_suite
$ fx build
```

(Disclaimer: if these build rules become out-of-date, please check the
[Build documentation](docs/development/workflows) and update this README!)

## Running

Use the `locate` tool to find the fuchsia-pkg URLs of these components:

```bash
$ fx shell locate hello_world
```

Pick the URL of a component, and provide it to `run`:

```bash
$ fx shell run 'fuchsia-pkg://fuchsia.com/hello_world_rust#meta/hello_world_rust.cm'
```

Make sure you have `fx serve` running in another terminal so your component can
be installed!

## Testing

To run one of the test components defined here, use `fx run-test` with the
package name:

```bash
$ fx run-test hello_world_rust_tests
```

To run the tests as a Components Framework v2 component, use `fx shell run-test-suite`:

```bash
fx shell run-test-suite 'fuchsia-pkg://fuchsia.com/hello_world_rust_tests#meta/hello_world_rust_bin_test.cm'
```
