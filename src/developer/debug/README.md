# Debug

This directory contains the components of the Fuchsia debugger.

### User documentation

Please see the debugger [setup](../../../garnet/docs/debugger.md) and
[usage](../../../garnet/docs/debugger_usage.md) documentation.

### Subdirectories

  * `debug_agent`: Code for the stub that runs on a Fuchsia system that
    performs the backend operations.

  * `ipc`: Definitions and associated code for the IPC protocol between the
    zxdb frontend and the debug agent.

  * `shared`: The set of code that is shared between the debug agent and the
    zxdb frontend.

  * `zxdb`: The debugger frontend that runs on the developer's host computer
    (Linux or Mac).
