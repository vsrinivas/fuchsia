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

Running the following `fx set` command will include all of the tests in the
build.
```
fx set core.x64 --with-base=//src/session,//src/session/bin/session_manager:session_manager.config --with //src/session:tests
```

### Session Manager Tests

You can run the session manager integration test using the following command.
```
fx run-test session_manager_integration_tests
```

### Example Session Tests

This `fx set` includes all example sessions and their associated helper
components:

* `input_session`
* `graphical_session`
* `element_session`
  * `element_proposer`
  * `simple_element`

The tests for these can be run using a command that follows this pattern:
```
fx run-test <component_name>_tests
```
For example, to run the input session tests the command would be
```
fx run-test input_session_tests
```
