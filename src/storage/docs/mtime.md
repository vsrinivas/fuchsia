# Modification Time Semantics

Many filesystems support mtime (modification time) as an attribute on files.  Intuitively, mtime is
updated when a file's contents change, but intuition is easily beguiled and the actual semantics of
mtime are much more subtle.  This document describes the semantics of mtime on Fuchsia filesystems.

Warning: The semantics described in this document aren't implemented yet.  This document serves as a
contract for ongoing implementation work.  If you come across this document and don't work on
Storage, please ask us about the current state of mtime affairs, rather than assuming this document
is an accurate description of the current state.

## Properties of modification time

The following properties are guaranteed for mtime on Fuchsia:

* If at least one write to a file is persisted to disk before the next `fsync` or the next reboot
  (graceful or not), mtime is guaranteed to be updated at least once.  That said, the value of mtime
  is not guaranteed to *change* in this update; see non-monotinicity below.

None of the following are guaranteed properties of mtime on Fuchsia:

* Mtime is not guaranteed to be updated for every write to a file.  Concretely, in the presence of a
  writeback cache, several writes to a file may be queued up and flushed together at a later time;
  in this case only one mtime update would need to be performed.

* Mtime being updated does not guarantee that its corresponding file write was persisted to disk.
  If the filesystem exits uncleanly, it is possible for an mtime update to be observed without the
  write that induced that mtime update.

  Concretely, the following scenario is possible:

  ```c
  write(…);  // A
  fstat(…);  // <-- see time T1
  write(…);  // B
  fstat(…);  // <-- see time T2
  // Later, after an ungraceful shutdown
  read(…);  // <-- see A
  read(…);  // <- Don't see B
  stat(…);  // see time T2
  ```

* Mtime is not guaranteed to be monotonically increasing.  In practice, Fuchsia-native filesystems
  use the UTC time source, which does not guarantee monotonicity; see
  <https://fuchsia.dev/fuchsia-src/concepts/time/utc/behavior>.

* Mtime has no guaranteed time precision.  In practice, Fuchsia-native filesystems use Zircon
  time objects which are nanosecond granularity, but there is no requirement for filesystems to
  store nanosecond-granularity timestamps.

* Mtime is not guaranteed to match the time source that an application has access to.  In practice,
  Fuchsia-native filesystems use the system UTC time source, but it is possible for components (or
  the filesystem) to use arbitrary time sources and there is no requirement for these time sources
  to be in sync.

## Suggested implementation of mtime in Fuchsia filesystems

The VFS should generally drive the policy of when to update mtime, and filesystems should offer a
mechanism to update the mtime on a file.

Ideally, filesystems should offer the ability to atomically update mtime with a file write.  If this
is not possible, the filesystem should ensure (via journalling) that the mtime update is persisted
whenever the write is persisted (i.e. it should not be possible to observe the write without the
mtime update).

In the future, when we have writeback caching, the VFS will send one or more writes to the
filesystem as part of the asynchronous flush of cached writes.  The first of these writes for a
given file (during a given cache flush) should update the file mtime.
