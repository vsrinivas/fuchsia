# Routing Example

This directory contains an example of
[capability routing](/docs/concepts/components/component_manifests#capability-routing)
in [Component Framework](/docs/concepts/components/introduction.md).

## Building

If these components are not present in your build, they can be added by
appending `--with //examples` to your `fx set` command. For example:

```bash
$ fx set core.x64 --with //examples --with //examples:tests
$ fx build
```

## Running

Use `ffx component create` to create the component instances inside a restricted
realm for development purposes:

```bash
$ ffx component create /core/ffx-laboratory:echo_realm fuchsia-pkg://fuchsia.com/components-routing-example#meta/echo_realm.cm
```

Start the client component instance by passing its moniker to
`ffx component bind`:

```bash
$ ffx component bind /core/ffx-laboratory:echo_realm/echo_client
```

When the above command is run, you can see the following output with `fx log`:

```
[echo_client] INFO: Server response: Hello Fuchsia!
```

## Testing

Integration tests for echo server are available in the `echo_integration_test`
package. Use the `ffx test run` command to run the tests on a target device:

```bash
$ ffx test run fuchsia-pkg://fuchsia.com/echo_integration_test#meta/echo_integration_test.cm
```

You should see the integration tests execute and pass:

```
Running test 'fuchsia-pkg://fuchsia.com/echo_integration_test#meta/echo_integration_test.cm'
[RUNNING]	echo_integration_test
[PASSED]	echo_integration_test

1 out of 1 tests passed...
```
