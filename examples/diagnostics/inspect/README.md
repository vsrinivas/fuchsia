# Inspect examples

This directory contains examples components using the Inspect libraries.
For more details on Inspect, see
[component inspection](/docs/development/diagnostics/inspect)

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
URL to `run`, then `bind` to the client component:

-  **C++**

    ```bash
    $ ffx component run 'fuchsia-pkg://fuchsia.com/inspect-example-cpp#meta/echo_realm.cm'
    $ ffx component bind '/core/ffx-laboratory:echo_realm/echo_client'
    ```

-  **Rust**

    ```bash
    $ ffx component run 'fuchsia-pkg://fuchsia.com/inspect-example-rust#meta/echo_realm.cm'
    $ ffx component bind '/core/ffx-laboratory:echo_realm/echo_client'
    ```

This starts the `echo_client` component, which sends a request message to the
`echo_server` for each argument. Each request is tracked by `echo_server` using
Inspect.

Use `ffx inspect show` to review the metrics collected by `echo_server`:

```bash
$ ffx inspect show 'core/ffx-laboratory\:echo_realm/echo_server'
core/ffx-laboratory\:echo_realm/echo_server:
  metadata:
    filename = fuchsia.inspect.Tree
    component_url = #meta/echo_server.cm
    timestamp = 2601204210478
  payload:
    root:
      bytes_processed = 14
      request_count = 1
      fuchsia.inspect.Health:
        start_timestamp_nanos = 2169252928627
        status = OK
```

You can run `ffx component bind` multiple times to watch the server metrics
increment with each request.

## Testing

To run one of the test components defined here, provide the package name to
`fx test`:

-  **C++**

    ```bash
    $ fx test inspect-example-cpp-tests
    ```

-  **Rust**

    ```bash
    $ fx test inspect-example-rust-tests
    ```

## Additional Inspect examples

-   [Inspect codelab](codelab/README.md): Interactive codelab demonstrating
    additional Inspect concepts.
-   [Inspect Dart](dart/README.md): A simple module that demonstrates Inspect
    usage from within Flutter/Dart.
-   [Ergonomic Inspect](rust-ergonomic/README.md): Demonstration of the
    `fuchsia_derive_inspect` Rust crate.
