# session_control tool
Reviewed on: 2020-02-04

`session_control` is a Fuchsia command line developer tool which allows developers to connect to the [`session_manager`](/src/session/bin/session_manager/README.md) and send it commands at runtime.

## Building

This project can be added to builds by including `--with-base //src/session/tools:all` to the `fx set` invocation.

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
$ fx run-test session_control_tests
```

## Source layout

The entrypoint and implementation are located in `src/main.rs`. Unit tests are co-located with the code.

## Note on implementation

`session_control` connects to control services published by `session_manager` at runtime. One such service is `fuchsia.session.Launcher`.

Due to the (current) inability to expose services directly from a v2 component such as `session_manager` via `/hub`, `session_manager` exposes these services to its parent special-purpose implementation of component manager (`component_manager_sfw`), with `sysmgr` providing service discovery as defined in [`session_manager.config`](/src/session/bin/session_manager/meta/session_manager.config).