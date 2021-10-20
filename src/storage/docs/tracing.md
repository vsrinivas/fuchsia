# Tracing the storage stack

This document describes the storage stack's integration with Fuchsia's
[tracing system](/docs/concepts/tracing.md) system, and how you can use tracing
to investigate performance, interactions, and behaviour in the storage stack.

For general background on how to use tracing, see
[Recording a trace](/docs/development/tracing/tutorial/recording-a-fuchsia-trace.md).

## Quick Start

Run the following command to take a trace for investigating general storage
issues:

```
fx traceutil record --categories=storage,blobfs,kernel,kernel:sched,minfs --duration 10s
```

If you want to see detailed flows for page faults and user pager events, add the
`kernel:vm` category as well:

```
fx traceutil record --categories=storage,blobfs,kernel,kernel:sched,kernel:vm,minfs --duration 10s
```

Once the trace is complete, an `.fxt` file will be generated. Load this file in
<https://ui.perfetto.dev> to get started.

## Storage Tracing

Fuchsia's tracing system mainly deals with [durations]{trace-durations} and
[flows]{trace-flows}. We can see both of these in use in the following example,
where durations are the blocks of execution and flows are the arrows pointing
between durations in different threads or processes:

![Example trace](example_trace.png)

### Trace durations

Trace durations are the most common type of instrumentation. They record how
long a given operation takes (measured both by wall time and CPU time).

Both blobfs and minfs have instrumentation for a variety of common filesystem
operations. For example, in blobfs, every block transaction is instrumented to
record the duration (
[source](https://cs.opensource.google/fuchsia/fuchsia/+/main:src/storage/blobfs/blobfs.h;l=97;drc=f14ae2556f5c35bf9f33f4cd7f1b6fb5a53dd80d)):

```
  zx_status_t Transaction(block_fifo_request_t* requests, size_t count) final {
    TRACE_DURATION("blobfs", "Blobfs::Transaction", "count", count);
    return block_device_->FifoTransaction(requests, count);
  }
```

Adding a new trace duration is simple. The `TRACE_DURATION` macro starts a new
duration which ends when the current scope ends. Durations can take zero or more
named paramenters, such as `count` in the example above. There is no performance
overhead for trace durations when tracing is disabled, so feel free to use these
liberally.

### Trace flows

Flow events allow us to connect durations across threads or processes. This is
useful for tracking asynchronous work. Currently, the block stack has basic
support for trace flow events. Block operations which are performed through
[libtransaction](/zircon/system/ulib/fs/transaction/) will automatically be
annotated with trace flow IDs. When interacting directly with the block FIFO,
trace flow IDs must be manually assigned (see
[trace.h](/zircon/system/ulib/fs/transaction/trace.h) for details about how to
generate appropriate IDs).

When adding trace flows, all of the flow events must be contained within a
`TRACE_DURATION`, and the category and name of each of the flow events must
match up with the other flows. For example, the block request flow events are
annotated as such:

```
// The start of a block transaction. There must be exactly one of these per ID.
TRACE_FLOW_BEGIN("storage", "BlockOp", request.trace_flow_id);

// A step in a block transaction. We can have zero or more of these.
TRACE_FLOW_STEP("storage", "BlockOp", request.trace_flow_id);

// The end of a block transaction. There must be exactly one of these per ID.
TRACE_FLOW_END("storage", "BlockOp", request.trace_flow_id);
```

If we added another flow step, it *must* match the above definition exactly to
be considered part of the overall flow. (The duration that encloses the flow
step can be arbitrarily named, which is how we add more information about where
we are in the flow.)

### List of trace categories

The following categories are useful for storage work:

| Category     | Summary |
| ------------ | ------------------------------------------------------------- |
| kernel       | Kernel trace events. You almost always want this, since this provides the metadata for processes/threads. |
| kernel:sched | Kernel scheduler information. You almost always want this, as it provides thread state. |
| kernel:vm    | Trace events for page faults, which is useful for investigating fault performance. (This allows you to see the faulting thread.) |
| blobfs       | Blobfs-specific trace events. |
| minfs        | Minfs-specific trace events. |
| zxcrypt      | zxcrypt-specific trace events. |
| storage      | General storage trace events (block IO flows, for example) |

## Troubleshooting

### There are only a few seconds worth of data in my trace

Most likely the trace buffer was too small. You can increase the trace buffer
size with the `--buffer-size` flag (units are MB):

```
fx traceutil record --categories=storage,blobfs,kernel,kernel:sched --duration 5s  --buffer-size 64
```
