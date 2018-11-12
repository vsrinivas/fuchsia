# Intel Hardware Performance Monitor Data Collection

See chapters 18,19 of the Intel Architecture Software Developer's Manual.

## IOCTLs

Several ioctls are provided to control performance data collection.

### *ioctl_cpuperf_get_properties*

```
ssize_t ioctl_cpuperf_get_properties(int fd, cpuperf_properties_t* props);
```

Return various aspects of PMU properties in |*props|.

Returns 0 on success or a negative error code.

### *ioctl_cpuperf_alloc_trace*

```
ssize_t ioctl_cpuperf_alloc_trace(int fd, const ioctl_cpuperf_alloc_t* alloc);
```

Allocate various resources needed to perform a trace.
This must be called before staging a configuration and starting a trace.
This must be called while tracing is stopped.

Returns 0 on success or a negative error code.

### *ioctl_cpuperf_free_trace*

```
ssize_t ioctl_cpuperf_free_trace(int fd);
```

Free all resources allocated by a preceding all to
*ioctl_cpuperf_alloc_trace()*.
This must be called while tracing is stopped.

Returns 0 on success or a negative error code.

### *ioctl_cpuperf_get_alloc*

```
ssize_t ioctl_cpuperf_get_alloc(int fd, cpuperf_alloc_t* alloc);
```

Fetch the trace configuration passed in to a preceding call to
*ioctl_cpuperf_alloc_trace()*.

Returns 0 on success or a negative error code.

### *ioctl_cpuperf_stage_config*

```
ssize_t ioctl_cpuperf_stage_config(int fd, const cpuperf_config_t* config);
```

Configure data collection. |*config| specifies the events to collect
and the rate at which to collect them.
This must be called while tracing is stopped.

Returns 0 on success or a negative error code.

### *ioctl_cpuperf_get_config*

```
ssize_t ioctl_cpuperf_get_config(int fd, cpuperf_config_t* config);
```

Fetch the configuration passed in to a preceding call to
*ioctl_cpuperf_stage_config()*.

Returns 0 on success or a negative error code.

### *ioctl_pmu_get_buffer_handle*

```
ssize_t ioctl_cpuperf_get_buffer_handle(
    int fd, const ioctl_cpuperf_buffer_handle_req_t* rqst,
    zx_handle_t* handle);
```

Fetch the handle of the VMO for the given descriptor.
Each CPU is given a separate VMO and the descriptor is the cpu's number.
This must be called while tracing is stopped.

Returns 0 on success or a negative error code.

### *ioctl_cpuperf_start*

```
ssize_t ioctl_cpuperf_start(int fd);
```

Start data collection.
This must be called while tracing is stopped.

Returns 0 on success or a negative error code.


### *ioctl_cpuperf_stop*

```
ssize_t ioctl_cpuperf_stop(int fd);
```

Stop tracing and collect any remaining data from each cpu.
This may be called even if tracing is already stopped.

Returns 0 on success or a negative error code.

## Usage

Here's a sketch of typical usage:

1) *ioctl_cpuperf_alloc_trace(fd, &alloc)*
2) *ioctl_cpuperf_stage_config(fd, &config)*
3) *ioctl_cpuperf_start(fd)*
4) launch program one wishes to trace
5) *ioctl_cpuperf_stop(fd)*
6) fetch handles for each vmo, and process data
7) *ioctl_cpuperf_free_trace(fd)* [this will free each buffer as well]
