# Fxfs on-disk versioning

All "top level" data structures in Fxfs are prefixed with a version number when
stored on disk. This includes the superblock, journal, and layer files as well
as metadata about the allocator and object stores.

This directory contains the code to deserialize older versions of structures and
upgrade those structures to the latest versions.

Our versioning is based on a single Fxfs-wide version number stored in
`LATEST_VERSION` consisting of a 24-bit major and 8-bit minor version.

## How to perform on-disk structural changes

There is some minimal work required by users (developers) when these structures
are changed.

1. Copy the previous version of the struct into `types.rs`. Where appropriate,
   drop comments and restrict visibility of fields to private.
2. Implement `From<OldVersion> for NewVersion` for their new version.
3. Update the `versioned_type!` invocation with new version as an open ended range
   at the start of the list. e.g.

   ```
   versioned_type! {
     4.. => Foo,
     2.. => FooV1
   }
   ```

   Note that the version and the type name suffix don't need to correspond. In
   the above example, it is invalid to decode Foo at version 1.
4. Bump the major component of `LATEST_VERSION` and set the minor component to zero.

## How to perform non-structural changes

Occasionally we might have need to change an algorithm. (For example, the way
that a bloom filter or index structure works.) In such cases where the format of
structures do not change but we wish to identify a filesystem as being written
with a specific feature in place, we can bump the minor component of
`LATEST_VERSION`.

## What if I have a breaking format change?

There are two main reasons for this:

1. We eventually will want to remove old, unused code. We will do this
   via an as-yet undefined 'stepping stone' process. Devices will be required to
   upgrade through stepping stone releases of the filesystem at which point we will
   require a full (major) compaction. This major compaction rewrites all metadata
   at the latest version for that release. This means we can be sure that versions
   written two such stepping stones ago will not be in use and can be safely removed.

2. A rare need to break the on-disk format. e.g. changing checksum algorithm.
   In such a case we will likely have to write custom code to migrate data
   structures. The safest way to do this is likely to be to target a single
   'source version', which also lends itself well to the stepping stone process
   above. i.e. Migrate to version N as a stepping stone and then N+1 as a second
   stepping stone.

From the programmers point of view, the action is not much more than removing
the incompatible versions from the `versioned_type` stanza and removing any unused
struct versions and associated `From` trait implementations.

Any attempt to load a version too low to be supported will result in a runtime error
explaining the unsupported version.

## Golden Images

Golden images exist under "//src/storage/fxfs/testdata/".
They are small (<10kB), compressed fxfs images from various versions of the filesystem.

The images are generated via the command `fx fxfs create_golden` (for which code
lives under `fxfs/tools/src/`).

A host test loads the images and ensures that they can all be read.
The test also ensures that there is an image for the current `LATEST_VERSION`.
This test is part of CQ and will instruct the user to generate a new image if
the version is bumped without also generating one in the same CL.

Golden images are expected to exist until the versions they use are no longer
supported at which time they can simply be deleted from the testdata directory.
