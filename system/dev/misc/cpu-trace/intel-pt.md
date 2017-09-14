# Intel Processor Trace driver

See Chapter 36 of the Intel Architecture Software Developer's Manual.

## Trace modes

There are two modes of tracing:

- per cpu
- specified threads (not implemented yet)

Only one may be active at a time.

### Per CPU tracing

In this mode of operation each cpu is traced, regardless of what is
running on the cpu, except as can be controlled by PT configuration MSRs
(e.g., cr3 filtering, kernel/user, address filtering).

### Specified thread tracing

In this mode of operation individual threads are traced, even as they
migrate from CPU to CPU. This is achieved via the PT state save/restore
capabilities of the XSAVES and XRSTORS instructions.

Filtering control (e.g., cr3, user/kernel) is not available in this mode.
Address filtering is possible, but is still TODO.

## IOCTLs

Several ioctls are provided to control tracing.

### *ioctl_ipt_alloc_trace*

```
ssize_t ioctl_ipt_alloc_trace(int fd, const ioctl_ipt_trace_config_t* config);
```

Request the kernel allocate the needed internal data structures for
managing IPT.

Returns zero on success or a negative error code.

### *ioctl_ipt_free_trace*

```
ssize_t ioctl_ipt_free_trace(int fd);
```

Request the kernel free all internal data structures for managing IPT.

Returns zero on success or a negative error code.

### *ioctl_ipt_get_trace_config*

```
ssize_t ioctl_ipt_get_trace_config(int fd,
                                   const uint32_t* descriptor,
                                   ioctl_ipt_trace_config_t* out_config);
```

Return the trace configuration.

Returns *sizeof(\*out_config)* on success or a negative error code.

### *ioctl_ipt_alloc_buffer*

```
ssize_t ioctl_ipt_alloc_buffer(int fd,
                               const ioctl_ipt_buffer_config_t* config,
                               uint32_t* out_descriptor);
```

Allocate an IPT buffer defined by |config| and return its buffer descriptor
in *|\*out_descriptor|*.

The descriptor is just an integer to identify the buffer.
When tracing in cpu mode the descriptor is the number of the cpu
that the buffer will be assigned to.

Returns *sizeof(\*out_descriptor)* on success or a negative error code.

### *ioctl_ipt_get_buffer_config*

```
ssize_t ioctl_ipt_get_buffer_config(int fd,
                                    const uint32_t* descriptor,
                                    ioctl_ipt_buffer_config_t* out_config);
```

Return the trace configuration.

Returns *sizeof(\*out_config)* on success or a negative error code.

### *ioctl_ipt_get_buffer_info*

```
ssize_t ioctl_ipt_get_buffer_info(int fd,
                                  const uint32_t* descriptor,
                                  ioctl_ipt_buffer_info_t* out_info);
```

Return info of the resulting trace of the specified buffer.
Currently this is the place in the buffer where tracing stopped,
treating the set of buffers as one large virtual buffer.
If not using circular buffers then this is the amount of data captured.
If using circular buffers then this is where tracing stopped.

Returns *sizeof(\*out_data)* on success or a negative error code.

### *ioctl_ipt_get_chunk_handle*

```
ssize_t ioctl_ipt_get_chunk_handle(int fd,
                                   const ioctl_ipt_chunk_handle_req_t* req,
                                   zx_handle_t* out_handle);
```

Return the handle of the requested VMO with buffer data.
IPT buffers can be spread out over multiple VMOs as specified when the
buffer was configured. This call returns the handle of one of them.
Multiple calls are required to fetch the handle for each VMO.

Returns *sizeof(\*out_handle)* on success or a negative error code.

### *ioctl_ipt_free_buffer*

```
ssize_t ioctl_ipt_free_buffer(int fd,
                              const uint32_t* descriptor);
```

Free the specified buffer and all associated VMOs.
The caller still needs to free its own handles of the VMOs.

Returns zero on success or a negative error code.

### *ioctl_ipt_start*

```
ssize_t ioctl_ipt_start(int fd);
```

Begin tracing.
One buffer must have already been allocated for each cpu
with *ioctl_ipt_alloc_buffer*.

Returns zero on success or a negative error code.

### *ioctl_ipt_stop*

```
ssize_t ioctl_ipt_stop(int fd);
```

Stop tracing.

Internally, status of the tracing of each cpu is collected for later
retrieval with *ioctl_ipt_get_buffer_info()*.

Returns zero on success or a negative error code.

## Usage

Here's a sketch of typical usage when tracing in cpu mode.

1) *ioctl_ipt_alloc_trace()*
2) allocate buffers for each cpu
3) *ioctl_ipt_start()*
4) launch program one wishes to trace
5) *ioctl_ipt_stop()*
6) fetch buffer data for each cpu
7) fetch handles for each vmo in each buffer
8) *ioctl_ipt_free_trace()*
9) post-process
10) free buffers

## Notes

- We currently only support Table of Physical Addresses mode so that
we can also support stop-on-full behavior in addition to wrap-around.

- Each cpu has the same size trace buffer.

- While it's possible to allocate and configure buffers outside of the driver,
this is not done so that we have control over their contents. ToPA buffers
must have specific contents or Bad Things can happen.

## TODOs (beyond those in the source)

- support tracing individual threads using xsaves

- handle driver crashes
  - need to turn off tracing
  - need to keep buffer/table vmos alive until tracing is off
