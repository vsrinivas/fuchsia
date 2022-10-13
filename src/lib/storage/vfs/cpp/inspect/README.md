
# VFS Inspect Library

## Overview

The VFS Inspect library allows filesystems to create an inspect tree with a standardized layout.
This is done by implementing getters/callbacks to return the structs defined in `inspect_data.h`.
This data is then mapped to inspect properties using `fs_inspect::CreateTree` in `inspect_tree.h`.

Currently, to create a standard inspect tree, filesystems must provide getters/callbacks providing
the following data types defined in `inspect_data.h`:

 - `fs_inspect::InfoData` - general information about the filesystem instance (`fs.info`)
 - `fs_inspect::UsageData` - current filesystem resource utilization (`fs.usage`)
 - `fs_inspect::FvmData` - information from the volume manager (`fs.fvm`)

In addition to providing a common set of properties, filesystems are free to attach detailed
information specific to their implementation.  This is done by implementing a callback to populate
a lazy Inspect node, which will be attached under `fs.detail`.

For a reference implementation that is fully thread-safe, see `BlobfsInspectTree` in
`src/storage/blobfs/blobfs_inspect_tree.h`.


TODO(fxbug.dev/85419): Support for tracking performance/latency and failure rates is in progress.

## Usage

Filesystems should first create an object to manage and query their inspect data.  This must be done
in a thread-safe manner as the inspect data will be queried asynchronously. The following example
shows how a filesystem might expose this data (note that locks have been omitted for brevity):

```
class FilesystemInspectData {
 public:
  // Example interface that a filesystem might use to set the inspect data.
  // NOTE: These methods *must* be thread-safe (for brevity, they are not in this example)!
  void SetInfo(const fs_inspect::InfoData& info_data) { info_data_ = info_data; }
  void UpdateSpaceUsage(uint64_t used_bytes, uint64_t total_bytes) {
    usage_data_.used_bytes = used_bytes;
    usage_data_.total_bytes = total_bytes;
  }

  // Attach to and serve this tree on the given inspect root (defined below).
  void Attach(inspect::Inspector inspector);

 private:
  // Data types defined in `inspect_data.h` to be queried from callbacks (see `Attach()` below).
  fs_inspect::InfoData info_data_;
  fs_inspect::UsageData usage_data_;
  fs_inspect::FvmData fvm_data_;

  // The Inspector on which the tree was attached.
  inspect::Inspector inspector_;

  // Maintains ownership of the nodes and callbacks created via `fs_inspect::CreateTree`.
  // Must not outlive any data the callbacks reference.
  fs_inspect::FilesystemNodes inspect_nodes_;
} inspect_data;
```

Filesystems can then populate an inspect tree by passing callbacks returning the inspect data to
`fs_inspect::CreateTree`.  This creates and returns ownership of inspect nodes that map these data
types to their respective inspect properties.  For example:

```
// Create and serves filesystem inspect data in a standardized layout.
void FilesystemInspectData::Attach(inspect::Inspector inspector) {
  inspector_ = std::move(inspector);
  // NOTE: These callbacks *must* be thread safe (for brevity, they are not in this example)!
  fs_inspect::NodeCallbacks callbacks = {
    .info_callback = [this]{ return this->info_data_; },
    .usage_callback = [this]{ return this->usage_data_; },
    .volume_callback = [this]{ return this->volume_data_; },
  }
  // Once CreateTree returns, the above callbacks may be invoked asynchronously.
  inspect_nodes_ = fs_inspect::CreateTree(inspector_.GetRoot(), std::move(callbacks));
}
```

Once `CreateTree` is called, the callbacks provided will be invoked each time the inspect data is
queried.  The callbacks will only stop being invoked when the nodes go out of scope, thus detaching
them from the tree (e.g. by setting `inspect_nodes_ = {}` or when its destructor is called).

For a more detailed non-trivial implementation that is fully thread-safe, see `BlobfsInspectTree` in
`src/storage/blobfs/blobfs_inspect_tree.h`.

## Filesystem Testing

A filesystem's inspect tree can be tested via `fs_test` by enabling the `supports_inspect` option.
This will validate that the inspect tree hierarchy is consistent and that basic information is
reported correctly.  See `src/storage/fs_test/inspect.cc` for details.
