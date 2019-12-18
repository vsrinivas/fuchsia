# Session Manager Usage Guide

## Build a configuration that boots into a session

```
fx set core.x64 --with-base=//src/session,//src/session/bin/session_manager:session_manager.config
fx build
fx reboot
```

You can specify which session to launch in [`session_manager.cml`](session_manager/meta/session_manager.cml):
```
"args": [ "-s", "fuchsia-pkg://fuchsia.com/element_session#meta/element_session.cm" ],
```

If no session URL is specified, `session_manager` waits for a client to connect
to `fuchsia.session.Launcher` to launch a session.

To launch `session_manager` manually (e.g., if you don't include
`session_manager.config` in `--with-base`) run:
```
fx shell run fuchsia-pkg://fuchsia.com/component_manager_sfw#meta/component_manager_sfw.cmx fuchsia-pkg://fuchsia.com/session_manager#meta/session_manager.cm
```

TODO(37237): Multiple calls to `run` will not work correctly when launching
graphical sessions ("display has been claimed by another compositor" - Scenic).
This will be resolved by introducing a command line tool for launching sessions
"properly."

## Run the tests

In order to include the tests in the build run the following `fx set`:
```
fx set core.x64 --with-base=//src/session,//src/session/bin/session_manager:session_manager.config --with //src/session:tests
```
The tests can then be run with:
```
fx run-test session_manager_integration_tests
```
