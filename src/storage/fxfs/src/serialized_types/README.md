# Fxfs on-disk versioning

All "top level" data structures in Fxfs are prefixed with a version number when
stored on disk. This includes the superblock, journal, and layer files as well
as metadata about the allocator and object stores.

This directory contains the code to deserialize older versions of structures and
upgrade those structures to the latest versions.

Our versioning is based on a single Fxfs-wide version number stored in
`LATEST_VERSION` consisting of a 24-bit major and 8-bit minor version.

## How to perform changes which don't affect storage formats or related structures (i.e. minor version changes)

(These are assumed to be non-breaking changes, if not, then please see below.)

Occasionally we might have need to change an algorithm, for example, the way
that a bloom filter or index structure works. In such cases where the format of
structures do not change but we wish to identify a filesystem as being written
with a specific feature in place, we can bump the minor component of
`LATEST_VERSION`.

## How to perform storage format changes / on-disk structural changes (i.e. major version changes)

These changes are always considered to be major version changes. There is some
housekeeping that developers need to do, which varies depending on whether the
change is considered to be a non-breaking change or a breaking change.

### Non-breaking changes

Non-breaking changes are changes where it is possible to migrate the filesystem
from a previous storage format version to the latest storage format version.

1. Make a copy the previous version of the struct and give it a new name
   (e.g. rename it from `Foo` to `FooV1`). Where appropriate, drop comments and
   restrict visibility of fields to private.
2. Implement `From<OldVersion> for NewVersion` for their new version, e.g.
   implement `From<FooV1> for Foo`, or use the Migrate derive macro.
3. Bump the major component of `LATEST_VERSION` and set the minor component to zero.
4. Update the `versioned_type!` invocation with the new major version as an open ended range
   at the start of the list. For example, if the new major version is 4 then change this:

   ```
   versioned_type! {
     2.. => Foo
   }
   ```

   to this:

   ```
   versioned_type! {
     4.. => Foo,   // The new struct used for Fxfx 4.x and above
     2.. => FooV1  // The old struct used for Fxfs 2.x and 3.x
   }
   ```

   Note that the version and the type name suffix don't need to correspond. In
   the above example, it is invalid to decode FooV1 (or Foo) at Fxfs version 1.

#### Examples of struct converters

Since these converters are sometimes deleted from the tree (e.g. deleted after a
recent breaking change), here are some historical examples of converters:

*  [SuperBlock](https://osscs.corp.google.com/fuchsia/fuchsia/+/a25f54b46ae210a7f78a2809ad744274ba89fd6e:src/storage/fxfs/src/object_store/journal/super_block.rs;dlc=6d3abc59e3a434d717bad94201eeb80dace7266e) converter

*  [JournalRecord](https://fuchsia-review.googlesource.com/c/fuchsia/+/667484/3/src/storage/fxfs/src/object_store/journal.rs#132) converter

### Breaking changes

Breaking changes are changes where it is not possible (or not practical) to migrate
the filesystem from a previous storage format to the latest storage format. For example,
switching encryption mechanisms could be a breaking change. These types of changes are
expected to be rare.

1. Update all `versioned_type!` invocations to drop older versions e.g. change this:

   ```
   versioned_type! {
     4.. => Foo,
     2.. => FooV1
   }
   ```

   to simply this:

   ```
   versioned_type! {
     4.. => Foo,
   }
   ```

   (Do this for all the structs in `types.rs`)
2. Delete any existing `From<FooV1> for Foo` Fxfs converters in the tree, as we
   won't need them anymore.
3. Similarly, delete any old structs (like `FooV1`).
4. Bump the major component of `LATEST_VERSION` and set the minor component to zero.
5. For any structs being changed in this breaking change, bump their version to
   match the latest version e.g. if the major component of `LATEST_VERSION` is now 4,
   then change this:

   ```
   versioned_type! {
     2.. => Foo,
   }
   ```

   to this:

   ```
   versioned_type! {
     4.. => Foo,
   }
   ```
5. Also bump the version of the `SuperBlock` to match the major component of
   `LATEST_VERSION` e.g. change this:

   ```
   versioned_type! {
     3.. => SuperBlock,
   }
   ```

   to this:

   ```
   versioned_type! {
     4.. => SuperBlock,
   }
   ```

### When to make breaking changes

There are two main reasons for making breaking changes:

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
