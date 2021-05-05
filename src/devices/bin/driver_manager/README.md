# Driver Manager

The Driver Manager is responsible for enumerating, loading, and managing the life cycle of device
drivers. Driver Manager also vends the /dev directory to the rest of the system so that others
can access drivers.

## Building and Running

Driver Manager is built and run in every product. It is launched on startup.

If you're looking for running an isolated Driver Manager for testing, please see
[isolated_devmgr](/src/lib/isolated_devmgr/README.md).


## Commandline Options

When running Driver Manager there are some commandline options that can change Driver Manager's
behavior. Normally these are set in the CML file when Driver Manager is run as a component, or
through isolated Driver Manager for testing.

### --driver_search_path=\<string\>

Load drivers from this directory. Can be specified multiple times to search multiple directories.
If this is not defined, the default will be used.

### --load_driver=\<string\>

Load a driver with this path. The specified driver does not need to be in a `driver_search_path`
directory.

### --log_to_debuglog=\<bool\>

Connect the stdout and stderr file descriptors for this program to a debuglog handle acquired with
fuchsia.boot.WriteOnlyLog.

### --no-exit-after-suspend=\<bool\>

Do not exit Driver Manager after suspending the system.

### --path-prefix=\<string\>

The path prefix for binaries, drivers, libraries, etc. This defaults to `/boot/`

### --sys-device-driver=\<bool\>

Use this driver as the sys_device driver.  If nullptr, the default will be used.

### --driver-runner-root-driver-url=\<string\>

Use the Driver Runner and launch this driver URL as the root driver.

## Kernel Commandline Options

The behavior of Driver Manager can also be changed by several kernel commandline options.
Please look at the list of [kernel commandline options](/docs/reference/kernel/kernel_cmdline.md)
and look for the options that start with `devmgr.*`
