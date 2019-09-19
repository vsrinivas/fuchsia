The ptysvc provides basic pseudoterminals to its clients.  It exposes a
fuchsia.hardware.pty.Device service interface that acts very similarly to
the UNIX 98 "/dev/ptmx".  Each connection made through this interface will be
communicating with a new unique pty server.

The ptysvc implementation uses a single-threaded asynchronous programming model.
