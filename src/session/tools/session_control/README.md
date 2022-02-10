# session_control tool
Reviewed on: 2020-02-04

`session_control` is a Fuchsia command line developer tool which allows developers to connect to the [`session_manager`](/src/session/bin/session_manager/README.md) and send it commands at runtime.

## Building

To add this project to your build, append `--with-base //src/session/tools` to the `fx set` invocation.

It is also included in the larger `--with-base //src/session` target.

Once you have rebuilt (with `fx build`) be sure to re-pave your device.

## Running

Run the `session_control` tool from the command line on your workstation:

```
$ fx shell session_control --help
```

## Testing

Unit tests for `session_control` are included in the `session_control_tests` package.

```
$ fx test session_control_tests
```

## Source layout

The entrypoint and implementation are located in `src/main.rs`. Unit tests are co-located with the code.
