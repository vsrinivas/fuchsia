# Failed routing Example

This directory contains an example of failed
[capability routing](/docs/concepts/components/component_manifests#capability-routing)
in [Component Framework](/docs/concepts/components/introduction.md).

## Building

If these components are not present in your build, they can be added by
appending `--with //examples` to your `fx set` command. For example:

```bash
$ fx set core.x64 --with //examples
$ fx build
```

## Running

Use `ffx component create` to create the component instances inside a restricted
realm for development purposes:

-   **C++**

    ```bash
    $ ffx component create /core/ffx-laboratory:echo_realm fuchsia-pkg://fuchsia.com/components-routing-failed-example-cpp#meta/echo_realm.cm
    ```

-   **Rust**

    ```bash
    $ ffx component create /core/ffx-laboratory:echo_realm fuchsia-pkg://fuchsia.com/components-routing-failed-example-rust#meta/echo_realm.cm
    ```

Start the client component instance by passing its moniker to
`ffx component start`:

```bash
$ ffx component start /core/ffx-laboratory:echo_realm/echo_client
```

When the above command is run, you can see the component framework error messages
with `fx log`. The `Echo` protocol request fails due to a routing error:

```
[echo_client] WARNING: Required protocol `fidl.examples.routing.echo.Echo` was not available for target component `/core/ffx-laboratory:echo_realm/echo_client`:
A `use from parent` declaration was found at `/core/ffx-laboratory:echo_realm/echo_client` for `fidl.examples.routing.echo.Echo`, but no matching `offer` declaration was found in the parent
```

The `Echo2` protocol request fails due to an issue starting the component:

```
[component_manager] WARN: Failed to start component `fuchsia-pkg://fuchsia.com/components-routing-failed-example#meta/echo_server_bad.cm`:
unable to load component with url "fuchsia-pkg://fuchsia.com/components-routing-failed-example#meta/echo_server_bad.cm":
error loading executable: "reading object at \"bin/routing_failed_echo_server_oops\" failed: A FIDL client's channel to the service (anonymous) File was closed: NOT_FOUND
```

After running the example, you can remove the example realm using
`ffx component destroy`:

```bash
$ ffx component destroy /core/ffx-laboratory:echo_realm
```
