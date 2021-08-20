# Time examples

This directory contains examples of Fuchsia-specific time operations.
For more details on handling time in Fuchsia, see the
[documentation](/docs/concepts/time/overview.md).

## Building

To add this project to your build, append `--with //examples/time` to your
`fx set` invocation.

If you do not already have one running, start a package server so the example
components can be resolved from your device:

```bash
$ fx serve
```

## Running

To run one of the example components defined here, provide the full component
URL to `run`:

-  **C**

    ```bash
    $ ffx component run 'fuchsia-pkg://fuchsia.com/time-examples#meta/c-time-example.cm'
    ```

-  **C++**

    ```bash
    $ ffx component run 'fuchsia-pkg://fuchsia.com/time-examples#meta/cpp-time-example.cm'
    ```

-  **Rust**

    ```bash
    $ ffx component run 'fuchsia-pkg://fuchsia.com/time-examples#meta/rust-time-example.cm'
    ```

You can see the messages printed by the sample using `fx log`.
