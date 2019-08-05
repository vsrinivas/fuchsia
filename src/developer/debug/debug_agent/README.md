# Debug agent

This is the code for the backend of the Fuchsia debugger. This backend runs
on the target Fuchsia computer (Linux or Mac) and talks via IPC (code in
[../ipc](../ipc)) to the zxdb frontend (code in [../zxdb](../zxdb)) running on
the developer's workstation.

### User documentation

Please see the debugger [setup](../../../../garnet/docs/debugger.md) and
[usage](../../../../garnet/docs/debugger_usage.md) documentation.

### Manual testing

When changing anything related to process launching:

  * `run /boot/bin/ps`
  * `run -c fuchsia-pkg://fuchsia.com/debug_agent#meta/debug_agent.cmx` _or some other component
     URL._
  * `attach some_filter`  _When a process by that name already exists._
  * A new process caught by filter (technically 2 handles this, but it's good to be explicit).
  * `attach 56421`    _Attach by PID and name are different code paths in the agent!_
