# Routing Example

This directory contains an example of
[capability routing](/docs/concepts/components/component_manifests#capability-routing)
in [Component Framework](/docs/concepts/components/introduction.md).

## Building

If these components are not present in your build, they can be added by
appending `--with //examples` to your `fx set` command. For example:

```
$ fx set core.x64 --with //examples --with //examples:tests
$ fx build
```

## Running

Use `ffx component run` to launch the components into a restricted realm
for development purposes:

```
$ ffx component run fuchsia-pkg://fuchsia.com/components-routing-example#meta/echo_realm.cm
```

When the above command is run, you can see the following output with `fx log`:

```
[echo_client] INFO: Server response: Hello Fuchsia!
```

## Testing

Integration tests for echo server are available in the `echo_integration_test`
package. Use the `ffx test run` command to run the tests on a target device:

```
$ ffx test run fuchsia-pkg://fuchsia.com/echo_integration_test#meta/echo_integration_test.cm
```

You should see the integration tests execute and pass:

```
Running test 'fuchsia-pkg://fuchsia.com/echo_integration_test#meta/echo_integration_test.cm'
[RUNNING]	echo_integration_test
[PASSED]	echo_integration_test

1 out of 1 tests passed...
```
