# Routing Example Using Subpackages

This directory contains additional examples of
[capability routing](/docs/concepts/components/component_manifests#capability-routing)
in [Component Framework](/docs/concepts/components/introduction.md), with
slightly modified packaging, using
[Subpackages (RFC-0154)](/docs/contribute/governance/rfcs/0154_subpackages.md), instead of bundling
all components into a single package.

## Building

If these components are not present in your build, they can be added by
appending `--with //examples` to your `fx set` command. For example:

<!--
TODO(fxbug.dev/102652): Remove --args='full_resolver_enable_subpackages=true'
when the feature flag is no longer needed:
-->

```bash
$ fx set core.x64 --with //examples --with //examples:tests \
  --args='full_resolver_enable_subpackages=true'
$ fx build
```

## Running

Use `ffx component run` (using either the C++ version or the Rust version) to
create the component instances inside a restricted realm (for development
purposes). The `run` command will also resolve and start the
`subpackaged_echo_realm` component automatically:

-   **C++**

```bash
$ ffx component run /core/ffx-laboratory:subpackaged_echo_realm fuchsia-pkg://fuchsia.com/subpackaged_echo_realm_cpp#meta/default.cm
```

-   **Rust**

```bash
$ ffx component run /core/ffx-laboratory:subpackaged_echo_realm fuchsia-pkg://fuchsia.com/subpackaged_echo_realm_rust#meta/default.cm
```

Start the client component instance by passing its moniker to
`ffx component start`:

```bash
$ ffx component start /core/ffx-laboratory:subpackaged_echo_realm/echo_client
```

When the above command is run, you can see the following output with `fx log`:

```
[echo_client] INFO: Server response: Hello Fuchsia!
```

After running the example, you can remove the example realm using
`ffx component destroy`:

```bash
$ ffx component destroy /core/ffx-laboratory:subpackaged_echo_realm
```

## Testing

Integration tests using Subpackages are also available. Use the `ffx test run`
command to run the tests on a target device:

-   **C++**

    ```bash
    $ ffx test run fuchsia-pkg://fuchsia.com/subpackaged_echo_integration_test_cpp#meta/default.cm
    ```

-   **Rust**

    ```bash
    $ ffx test run fuchsia-pkg://fuchsia.com/subpackaged_echo_integration_test_rust#meta/default.cm
    ```

You should see the integration tests execute and pass:

```
Running test 'fuchsia-pkg://fuchsia.com/subpackaged_echo_integration_test#meta/echo_integration_test_rust.cm'
[RUNNING]	echo_integration_test
[PASSED]	echo_integration_test

1 out of 1 tests passed...
```
