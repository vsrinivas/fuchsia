What is F2FS?
=============
F2FS is the most commonly used log-structured file system in Linux. It supports
flash-friendly features such as adaptive logging, multi-head logging, fsync acceleration,
and node address translation.
For more information see: https://www.usenix.org/conference/fast15/technical-sessions/presentation/lee

Major Release
=============
1st release (Apr-06-2021)
-------------------------
* Features: mkfs, mount/umount, read, write, rmdir, mkdir, rename, fsync, lseek, link, unlink
* Remarks
 + Fuchsia base code
  ; Thu Mar 11 08:53:24 2021 Prashanth Swaminathan,76a08ad1474 [speak] Migrate to new component templates
 + There is no cache. Every request is handled as a synchronous operation.
 + fsync() triggers checkpointing
 + lock granularity: file operation
 + LFS is used for block allocation by default, and IPU is forced when the number of free sections is below 50%.
 + no background/foreground gc
 + disable the roll-forward recovery
 + 6 active logs
 + disable the ext-identify feature
 + block allocation: LFS, IPU
 + fsck do nothing, but it returns true
 + no discard

2nd release (May-28-2021)
-------------------------
* Features: truncate, ssr, fsync, recovery
* Remarks
 + Fuchsia base code
  ; Thu May 20 07:25:45 2021 Yilong Li, 02c0dfff0fdb
 + support the roll-forward recovery and file level fsync()
 + block allocation: LFS, IPU, SSR
 + truncate

3rd release (June-25-2021)
-------------------------
* Features: fsck, mount option, mkfs option
* all fs-tests and large-fs-tests passed
* Remarks
 + fsck (base: 7f35b548d4b)
 + support the ext-identify feature
 + support discard

4th release (July-30-2021)
-------------------------
* Features: vnode cache, inline dentry
* unit test coverage

5th release (August-27-2021)
-------------------------
* Features: dentry cache
* Landing
* Linux compatibility test coverage

6th release (September-30-2021 ~)
-------------------------
* Features: gc, node cache, pager support, mmap, wb, readahead, ... (as new fuchsia features coming)
* stress test coverage
* performance test

How to build
============
$ fx set core.x64 --with-base //bundles:tools --with-base //bundles:tests  --with-base third_party/f2fs  
(see third_party/f2fs/test_files/README.md for test)
