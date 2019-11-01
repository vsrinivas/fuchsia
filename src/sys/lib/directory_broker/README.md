# `directory_broker`

Reviewed on: 2019/10/31

`directory_broker` is a library that provides the ability to enter in nodes to a
`fuchsia_vfs_pseudo_fs::directory` with arbitrary routing logic. This is useful
for inserting nodes into a Rust pseudo fs that proxy to a different vfs.

This library is hopefully short-lived, as support for this should be added to
the `fuchsia_vfs_pseudo_fs` library directly.

## Building

This library should be available on all Fuchsia configurations.

## Using

`directory_broker` can be used by depending on the
`//src/sys/lib/directory_broker`
GN target.

`directory_broker` is not available in the SDK.

## Testing

No tests are available for `directory_broker`.

## Source layout

The implementation is in `src/lib.rc`.
