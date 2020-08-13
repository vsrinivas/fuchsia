# FS watchdog

watchdog provides a way to track progress of an operation. Each operation has
a time scope - a start time and estimated time to complete. When an operation
does not complete in its estimated time, watchdog can take a set of action
including printing messages.

By default, when an operation times out, watchdog prints stack traces of all
the threads in the process. On timeout, watchdog also calls operation specific
timeout handlers. To avoid flooding logs with backtraces, watchdog tries to
print backtraces only once per timed out operation.

Watchdog tracks operations with OperationTracker. OperationTracker describes
the operation properties like
* Name
* Timeout value
* Start time of the operation
* Timeout handler

OperationTracker is not owned by the Watchdog. This allows Tracker to be
created on stack(without malloc) or allows Tracker to be part of a different
class that implements the base class.
