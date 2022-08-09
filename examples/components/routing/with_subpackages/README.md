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
TODO(fxbug.dev/102652): Use the following more common example, instead of the
one below, when the feature flag is no longer needed:

$ fx set core.x64 --with //examples --with //examples:tests
-->

```bash
$ fx set core.x64 --args='full_resolver_enable_subpackages=true' \
    --with //examples/components/routing/with_subpackages
    --with //examples/components/routing/with_subpackages:tests
$ fx build
```

## Running

Use `ffx component create` to create the component instances inside a restricted
realm for development purposes:

-   **C++**

```bash
$ ffx component create /core/ffx-laboratory:subpackaged_echo_realm fuchsia-pkg://fuchsia.com/subpackaged-echo-cpp#meta/subpackaged_echo_realm.cm
```

-   **Rust**

```bash
$ ffx component create /core/ffx-laboratory:subpackaged_echo_realm fuchsia-pkg://fuchsia.com/subpackaged-echo-rust#meta/subpackaged_echo_realm.cm
```

Start the client component instance by passing its moniker to
`ffx component start`:

```bash
$ ffx component start /core/ffx-laboratory:subpackaged_echo_realm
```

This will start the `subpackaged_echo_realm` component, according to its CML
declarations in [meta/subpackaged_echo_realm.cml]. Since the CML declares the
`echo_client` child with `startup: "eager"`, the echo_client component will
start automatically, and invoke the `echo_server`, launching that component as
well.

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
    $ ffx test run fuchsia-pkg://fuchsia.com/subpackaged_echo_integration_test_cpp#meta/subpackaged_echo_integration_test.cm
    ```

-   **Rust**

    ```bash
    $ ffx test run fuchsia-pkg://fuchsia.com/subpackaged_echo_integration_test_rust#meta/subpackaged_echo_integration_test.cm
    ```

You should see the integration tests execute and pass:

```
Running test 'fuchsia-pkg://fuchsia.com/subpackaged_echo_integration_test#meta/echo_integration_test_rust.cm'
[RUNNING]	subpackaged_echo_integration_test
[PASSED]	subpackaged_echo_integration_test

1 out of 1 tests passed...
```
