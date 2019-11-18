# Hello World Components

This directory contains simple components to show how components are built
run, and tested in the system.

## Building

If these components are not present in your build, they can be added by
appending `--with //examples` to your `fx set` command. Eg:

```bash
$ fx set core.x64 --with //examples:tests
$ fx build
```

(Disclaimer: if these build rules become out of date please check the
[Build documentation](docs/development/workflows) and update this readme!)

## Running

To find the fuchsia-pkg URLs of these components the `locate` tool can be used:

```bash
$ fx shell locate hello_world
```

Pick the URL of a component, and provide it to `run`:

```bash
$ fx shell run 'fuchsia-pkg://fuchsia.com/rust_hello_world#meta/rust_hello_world.cmx'
```

Make sure you have `fx serve` running in another terminal so your component can
be installed!

## Testing

To run one of the test components defined here, use `fx run-test` with the
package name:

```bash
$ fx run-test rust_hello_world_tests
```

Or use the `locate` tool again and provide a URL to `run_test_component` on the
Fuchsia shell:

```bash
$ fx shell locate hello_world_tests
$ fx shell run 'fuchsia-pkg://fuchsia.com/rust_hello_world_tests#meta/rust_hello_world_tests.cmx'
```
