# Lifecycle Example

This directory contains example components that demonstrate management of child
components and Lifecycle event handling in
[Component Framework](/docs/concepts/components/introduction.md).

## Building

If these components are not present in your build, they can be added by
appending `--with //examples` to your `fx set` command. For example:

```bash
$ fx set core.x64 --with //examples
$ fx build
```

## Running

### Lifecycle event handler

Use `ffx component run` to launch this component into a restricted realm
for development purposes:

-   **C++**

    ```bash
    ffx component run /core/ffx-laboratory:lifecycle fuchsia-pkg://fuchsia.com/lifecycle-example-cpp#meta/lifecycle.cm
    ```

-   **Rust**

    ```bash
    $ ffx component run /core/ffx-laboratory:lifecycle fuchsia-pkg://fuchsia.com/lifecycle-example-rust#meta/lifecycle.cm
    ```

When the above command is run, you can see the following output with `fx log`:

```
[lifecycle] INFO: Lifecycle channel received.
[lifecycle] INFO: Awaiting request to close...
```

To stop the component, use `ffx component stop`:

```bash
$ ffx component stop /core/ffx-laboratory:lifecycle
```

When the above command is run, you can see the following output with `fx log`:

```bash
[lifecycle] INFO: Received request to stop.
```

### Lifecycle manager

Use `ffx component run` to launch this component into a restricted realm
for development purposes:

-   **C++**

    ```bash
    ffx component run /core/ffx-laboratory:lifecycle_manager fuchsia-pkg://fuchsia.com/lifecycle-example-cpp#meta/lifecycle_manager.cm
    ```

-   **Rust**

    ```bash
    $ ffx component run /core/ffx-laboratory:lifecycle_manager fuchsia-pkg://fuchsia.com/lifecycle-example-rust#meta/lifecycle_manager.cm
    ```

When the above command is run, you can see the following output with `fx log`:

```
[lifecycle_manager]][I] Starting lifecycle child instance.
[lifecycle_manager/lifecycle][I] Lifecycle channel received.
[lifecycle_manager/lifecycle][I] Awaiting request to close...
[lifecycle_manager][I] Sending request: Hello
[lifecycle_manager][I] Dynamic child instance created.
[lifecycle_manager][I] Server response: Hello
[lifecycle_manager][I] Dynamic child instance destroyed.
[lifecycle_manager][I] Sending request: Fuchsia
[lifecycle_manager][I] Dynamic child instance created.
[lifecycle_manager][I] Server response: Fuchsia
[lifecycle_manager][I] Dynamic child instance destroyed.
```

To stop the component and its children, use `ffx component destroy`:

```bash
$ ffx component destroy /core/ffx-laboratory:lifecycle_manager
```

When the above command is run, you can see the following output with `fx log`:

```bash
[lifecycle_manager/lifecycle][I] Received request to stop. Shutting down.
```