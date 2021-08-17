# Lifecycle Example

This directory contains an example component that handles Lifecycle events in
[Component Framework](/docs/concepts/components/introduction.md).

## Building

If these components are not present in your build, they can be added by
appending `--with //examples` to your `fx set` command. For example:

```bash
$ fx set core.x64 --with //examples
$ fx build
```

## Running

Use `ffx component run` to launch this component into a restricted realm
for development purposes:

```
$ ffx component run fuchsia-pkg://fuchsia.com/lifecycle-example#meta/lifecycle.cm
```

When the above command is run, you can see the following output with `fx log`:

```
[lifecycle] INFO: Lifecycle channel received.
[lifecycle] INFO: Awaiting request to close
```

To stop the component, use `ffx component stop`:

```
$ ffx component stop core/ffx-laboratory:lifecycle
```

When the above command is run, you can see the following output with `fx log`:

```
[lifecycle] INFO: Received request to stop, bye bye!
```
