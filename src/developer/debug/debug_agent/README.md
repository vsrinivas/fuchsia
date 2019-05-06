# Debug agent

This is the code for the backend of the Fuchsia debugger. This backend runs
on the target Fuchsia computer (Linux or Mac) and talks via IPC (code in
[../ipc](../ipc)) to the zxdb frontend (code in [../zxdb](../zxdb)) running on
the developer's workstation.

### User documentation

Please see the debugger [setup](../../../../garnet/docs/debugger.md) and
[usage](../../../../garnet/docs/debugger_usage.md) documentation.
