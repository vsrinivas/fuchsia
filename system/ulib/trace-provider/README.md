Trace Provider Library
======================

A static library for publishing a trace provider.  The trace manager
connects to trace providers to collect trace records from running programs.

To register the trace provider, the program must call `trace_provider_init()`
at some point during its startup sequence.  The trace provider will take
care of starting and stopping the trace engine in response to requests from
the trace manager.
