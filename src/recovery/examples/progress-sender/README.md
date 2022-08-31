# progress-sender

Progress-sender is a trivial component that demonstrates how a core based component can display a minimal UI while the system is running non-interactive processes.

This is meant as an example which other components can be based on.

## Building

To add this component to your build, append
`--with-base src/recovery/examples/progress-sender`
to the `fx set` invocation.

## Running

Use `ffx component run` to launch this component into a restricted realm
for development purposes:

```
$ ffx component run /core/ffx-laboratory:progress-sender fuchsia-pkg://fuchsia.com/progress-sender#meta/progress-sender.cm
```