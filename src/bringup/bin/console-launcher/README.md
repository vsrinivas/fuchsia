# console-launcher

The console-launcher component is responsible for launching the console on the
debug serial connection. This component will relaunch the console if it ever
quits.

This component can be configured by the the following kernel commandline
options:
- console.shell
- console.is_virtio
- console.path
- TERM

Console-launcher should be built and launched automatically in every
configuration. Console-launcher will decide if the console should be launched
depending on the kernel commandline arguments.

## Testing

Unit tests for console-launcher are available in the `console-launcher-tests`
package.

```
$ fx test console-launcher-tests
```

