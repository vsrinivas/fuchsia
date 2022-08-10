# Launching Filesystems on Fuchsia

Filesystems on Fuchsia are regular programs that run in userspace. They consume a block device and
a set of options, and produce an export directory with several protocols, first and foremost a
fuchsia.io.Directory representing the root of the filesystem at `/root`.

There are currently two ways to launch a filesystem. One way, the old way which is being
deprecated, is to launch the raw filesystem binary, and the other way, which filesystems are moving
to, is to launch as a component via the component framework. Both of these methods are supported by
fs_management libraries in C++ and Rust (the Rust implementation is pending - fxbug.dev/96036).

No matter how the filesystem is launched (the old way or the new way), all our platform filesystems
have the same structure to the export directory. The basic entries are -

```
/diagnostics
/fuchsia.fs.Admin
/root
```

`diagnostics` provides the inspect tree implementation, which has a bunch of statistics and
metrics. `fuchsia.fs.Admin` provides the ability to shut down a filesystem instance. `root` is the
root of the filesystem. Other platform filesystems may include additional entries. For example,
blobfs provides `/fuchsia.update.verify.BlobfsVerifier`.

## Launching filesystems with fs_management

Filesystems are launched using the `fs_management::Mount`, which takes a block device file
descriptor, an optional mount path, the disk format, options, and a launch callback. Mount returns
an RAII MountedFilesystem object with which you can Unmount and get access to the export directory.

The block device should be opened as read-write unless you configure the filesystem to be in
read-only mode. One option for block device paths to open is in `/dev/class/block`, which is an
enumerated list of all the block devices Fuchsia knows about (starting at `000` and counting up).
It's populated in the order that the drivers are bound, which is _not_ consistent across boots. The
other option is to use the topological path, which will depend on the drivers the storage uses (but
is otherwise consistent). Both of these can be found using `lsblk` on the command line. A couple of
examples -

```
/dev/sys/platform/00:00:2d/ramctl/ramdisk-0/block
/dev/sys/platform/pci/00:1f.2/ahci/sata0/block
```

If you are launching a filesystem on a ramdisk, you can use the ramdisk functions to get the
ramdisk path.

The `mount_path` is expected to be a non-existing path in your namespace. fs_management uses
fdio_namespace_bind to place the root of the filesystem at that path in your local namespace. Note:
This path is process local! Only your process sees the namespace, and a new one is created for
every process. See //docs/concepts/process/namespaces.md for more information.

The options struct, `fs_management::MountOptions`, contains a union of all possible filesystem
configuration. That is, not all filesystems support all the options. Make sure that the filesystem
you are launching supports the option you are using, or else it may fail to launch.

If the options provided don't include a `component_child_name`, then fs_management will launch it
using fdio_spawn_etc as a child process. This is the old way of launching filesystems. It's
supported by all of our platform filesystems. This approach uses the launch callback provided to
mount. In general, if you are mounting a filesystem, you should use `LaunchStdioAsync`, and if
you are using Fsck or Mkfs you should use `LaunchStdioSync`. See
//src/lib/storage/fs_management/cpp/launch.h for additional callback options.

If only the `component_child_name` is provided, then fs_management will assume the component is a
static child in the realm. It attempts to connect to it's exposed directory via
`fuchsia.component.Realm`. This approach doesn't use the launch callback provided to mount.

If both `component_child_name` and `component_collection_name` are provided, then fs_management
will assume the component is a dynamic child in the realm. It attempts to connect to it's exposed
directory via `fuchsia.component.Realm`, and if it fails because it can't find a component with the
given name, it attempts to launch a new instance, using a component url based on the disk format.

A shard is provided for cml files to include if they plan to launch filesystems as dynamic children
at `//src/lib/storage/fs_management/client.shard.cml`. The `component_collection_name` using this
shard is `fs-collection`.

Launching a filesystem using regular processes -

```cpp
fbl::unique_fd device_fd(open("/dev/class/block/001", O_RDWR));
ASSERT_TRUE(device_fd);
fs_management::MountOptions options;
auto fs = fs_management::Mount(std::move(device_fd), fs_management::kDiskFormatMinfs, options, fs_management::LaunchStdioAsync);
ASSERT_EQ(fs.status(), ZX_OK);
auto data = fs->DataRoot();
ASSERT_EQ(data.status(), ZX_OK);
auto binding = fs_management::NamespaceBinding::Create("/fs", std::move(*data));
ASSERT_EQ(binding.status(), ZX_OK);
// Now /fs points at the root of the filesystem.
```

Launching a filesystem using a component collection -

```cpp
fbl::unique_fd device_fd(open("/dev/class/block/001", O_RDWR));
ASSERT_TRUE(device_fd);
fs_management::MountOptions options {
  .component_child_name = "minfs",
  .component_collection_name = "fs-collection",
};
// LaunchStdioAsync doesn't matter here
auto fs = fs_management::Mount(std::move(device_fd), fs_management::kDiskFormatMinfs, options, fs_management::LaunchStdioAsync);
ASSERT_EQ(fs.status(), ZX_OK);
auto data = fs->DataRoot();
ASSERT_EQ(data.status(), ZX_OK);
auto binding = fs_management::NamespaceBinding::Create("/fs", std::move(*data));
ASSERT_EQ(binding.status(), ZX_OK);
// Now /fs points at the root of the filesystem.
```

## Launching a filesystem as a process

This section has a detailed description of how fs_management launches platform filesystem
processes. It's intended for filesystem developers - if you just want to mount a filesystem, in
code, see the previous section.

Platform filesystem processes take 2 startup handles and a handful of command line arguments. The
two startup handles are `PA_DIRECTORY_REQUEST` and `FS_HANDLE_BLOCK_DEVICE_ID`
(`PA_HND(PA_USER0,1)`), which are the server end of the export directory and the block device
handle, respectively.

The export directory should have the structure described at the beginning of this document.

The block device should be a handle to something that speaks `fuchsia.hardware.block.Block`. Most
block devices speak a handful of other protocols as well.

Filesystems can use protocols from their namespace, but they should fail gracefully if they are not
there. This is because many test environments don't construct consistent namespaces for the
filesystems they run. The most common production environment is fshost, which copies all it's
services to filesystems it launches, so using anything it uses in it's cml is probably fine.

## Launching a filesystem as a component

This section has a detailed description of how fs_management launches platform filesystem
components. It's intended for filesystem developers - if you just want to mount a filesystem, in
code, see the previous section.

There is currently one filesystem that supports being launched as a component, blobfs. The cml file
for blobfs is in //src/storage/bin/blobfs-component/meta/blobfs.cml.

When a filesystem component is launched, it starts in a partially configured state. They serve one
protocol - `fuchsia.fs.startup.Startup`. This is served from the path
`/<fs_component>/svc/fuchsia.fs.startup.Startup`. Filesystems need two things to run - a set of
options and a block device handle. This protocol provides methods for `Start`, as well as `Format`
and `Check`, which take the block device and a set of options as arguments.

`Start` returns once the filesystem is launched. Before this point, it's expected that filesystems
queue incoming open requests to be processed once `Start` is called. In blobfs, this is done by
selectively calling Serve on parts of the export directory at a time. If a `Start` call fails,
fs_management will attempt to destroy and restart the lifecycle component.

Filesystems that are launched as components are also expected to consume an extra startup handle,
PA_LIFECYCLE. This handle is the server end of a fuchsia.process.lifecycle.Lifecycle protocol,
which the component framework can use to send shutdown requests.
