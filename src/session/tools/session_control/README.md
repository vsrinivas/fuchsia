# Session Control

This directory contains a tool which allows developers to connect to the session
manager and send it commands.

In order to use the tool, you must include the following in your `fx set`:

```
--with //src/session/tools:all
```

or, for all of the session related code:

```
--with-base=//src/session:all
```

## Example Usage

From the command line on your workstation:

```
fx shell session_control -s <SESSION_URL>
```

## Note on Implementation

This tool connects to the session services from its environment. In order to do
so, the session manager exposes the service to the `component_manager` instance
it is running under. The session framework has a custom v1 `component_manager`
which exposes the service in its outgoing directory. The `session_manager.config`
then informs `sysmgr` to route requests for `fuchsia.session.Launcher` to the
component manager (and thus the session manager).