# Trace Provider Library

A static library for publishing a trace provider.  The trace manager
connects to trace providers to collect trace records from running programs.

To register the trace provider, the program must call `trace_provider_create()`
at some point during its startup sequence.  The trace provider will take
care of starting and stopping the trace engine in response to requests from
the trace manager.

## trace-provider-with-static-engine

This library exists for very special cases, and in general should not be used.

To use it, you will need to provide your own connection with trace-manager.
This library does not come with the fdio support that the "normal" version
of this library uses. See fdio_connect.cpp in this directory for boilerplate
of what needs to be done. Then when you have the handle to trace-manager,
call **trace_provider_create_with_name()**.

```
trace_provider_t* trace_provider_create_with_name(
        zx_handle_t to_service_h, async_dispatcher_t* dispatcher,
        const char* name);
```

You will need to add these dependencies to your BUILD.gn file:

```
    "//zircon/public/lib/trace-with-static-engine",
    "//zircon/public/lib/trace-provider-with-static-engine",
```
