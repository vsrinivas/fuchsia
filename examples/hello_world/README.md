# Hello World Components

This directory contains simple components to show how components are built,
run, and tested in the system.

## Building

If these components are not present in your build, they can be added by
appending `--with //examples` to your `fx set` command:

```bash
$ fx set core.x64 --with //examples --with //examples:tests
$ fx build
```

If you do not already have one running, start a package server so the example
components can be resolved from your device:

```bash
$ fx serve
```

## Running

To run one of the example components defined here, provide the full component
URL to `run`:

-  **C++**

    ```bash
    $ fx shell run 'fuchsia-pkg://fuchsia.com/hello-world-cpp#meta/hello-world-cpp.cmx'
    ```

-  **Rust**

    ```bash
    $ fx shell run 'fuchsia-pkg://fuchsia.com/hello-world-rust#meta/hello-world-rust.cmx'
    ```

## Testing

To run one of the test components defined here, provide the package name to
`fx test`:

-  **C++**

    ```bash
    $ fx test hello-world-cpp-unittests
    ```

-  **Rust**

    ```bash
    $ fx test hello-world-rust-tests
    ```
