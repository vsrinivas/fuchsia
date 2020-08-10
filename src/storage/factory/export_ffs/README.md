# FactoryFS Exporter

This library and binary create FactoryFS partitions by exporting existing directory structures.
FactoryFS is immutable, so using this tool is the only real way to create these partitions with
data in them.

The library has a single call, `export_directory`, which takes a DirectoryProxy, which is the
root of the directory tree you want to export, and a channel to a block device.

The binary can be included in the build by adding `--with //src/storage:tools` to your `fx set`
line. It takes two arguments - one, a path to a directory to export, two, a path to a block
device to write the data to. This binary is intended to be used for manual development and
debugging of the factoryfs library.

```
$ fx set core.x64 --with //src/storage:tools
$ fx build

# on fuchsia
$ export-ffs /path/to/export /dev/class/block/<id>
```
