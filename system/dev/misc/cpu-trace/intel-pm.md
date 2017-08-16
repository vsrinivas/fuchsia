# Intel Hardware Performance Monitor Data Collection

See Chapters 18,19 of the Intel Architecture Software Developer's Manual.

## IOCTLs

Several ioctls are provided to control performance data collection.

### *ioctl_ipm_get_state*

```
ssize_t ioctl_ipm_get_state(int fd, mx_x86_ipm_state_t* state);
```

Return various aspects of IPM state in |state|.

Returns 0 on success or a negative error code.

### *ioctl_ipm_init*

```
ssize_t ioctl_ipm_init(int fd);
```

Allocate various resources needed. This must be called before configuring
the hardware and collecting data.

Returns 0 on success or a negative error code.

### *ioctl_ipm_stage_simple_config*

```
ssize_t ioctl_ipm_stage_simple_config(int fd,
                                      ioctl_ipm_simple_config_t* config);
```

Configure data collection by specifying a mask of what data to collect.
The device driver will convert this to the needed configuration data.

Returns 0 on success or a negative error code.

### *ioctl_ipm_stage_cpu_config*

```
ssize_t ioctl_ipm_stage_cpu_config(int fd,
                                   ioctl_ipm_cpu_config_t* config);
```

Configure data collection by specifying the raw values for the hardware
configuration registers.

Returns 0 on success or a negative error code.

### *ioctl_ipm_get_cpu_config*

```
ssize_t ioctl_ipm_get_cpu_config(int fd,
                                 uint32_t *cpu,
                                 const mx_x86_ipm_config_t* config);
```

Fetch the configuration for |cpu|.

Returns 0 on success or a negative error code.

### *ioctl_ipm_get_cpu_data*

```
ssize_t ioctl_ipm_get_cpu_data(int fd,
                               uint32_t *cpu,
                               const mx_x86_ipm_counters_t* config);
```

Fetch the collected data for |cpu|.

Returns 0 on success or a negative error code.

### *ioctl_ipm_start*

```
ssize_t ioctl_ipm_start(int fd);
```

Start data collection.

Returns 0 on success or a negative error code.


### *ioctl_ipm_stop*

```
ssize_t ioctl_ipm_stop(int fd);
```

Stop tracing and collect current data from each cpu.

Returns 0 on success or a negative error code.

### *ioctl_ipm_free*

```
ssize_t ioctl_ipm_free(int fd);
```

Request the kernel free all internal data structures for managing IPM.

Returns 0 on success or a negative error code.

## Usage

Here's a sketch of typical usage:

1) ???

## Notes

- ???

## TODOs (beyond those in the source)

- ???
