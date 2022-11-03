What is F2FS?
=============
F2FS is the most commonly used log-structured file system in Linux. It supports
flash-friendly features such as adaptive logging, multi-head logging, fsync acceleration,
and node address translation.
For more information see: https://www.usenix.org/conference/fast15/technical-sessions/presentation/lee

Limitations
=============
* Features under development: mmap(shared), xattr, fallocate

How to test
=============
Fuchsia filesystem tests
-------------
* Build configuration for fs-tests
(femu runs with f2fs data partition if you add --args='data_filesystem_format="f2fs"')
> $ fx set core.x64 --with-base //src/storage/f2fs:tests

* Run Fuchsia filesystem test suite with f2fs (slow-fs-tests can take more than 5 minutes.)
> $ fx test f2fs-fs-tests f2fs-slow-fs-tests

* Only run f2fs unit tests
> $ fx test f2fs-unittest

Linux compatibility tests (EXPERIMENTAL)
-------------
* Prerequisite
> Linux kernel built with f2fs
> $ apt install f2fs-tools (on Debian/Ubuntu)

* Build configuration for fs-tests
> $ fx set core.x64 --with-base //src/storage/f2fs/test/compatibility:f2fs-compatibility-tests

* Run Linux compatibility tests
> $ fx test f2fs-compatibility-test

Debian guest based Linux compatibility tests (EXPERIMENTAL)
-------------
* Prerequisite
> Generate Linux image

> $ ./src/virtualization/packages/debian_guest/build-image.sh \
>   prebuilt/virtualization/packages/debian_guest/images/x64 x64

* Build configuration
> $ fx set core.x64 --with //src/storage/f2fs:tests \
>   --with //src/storage/f2fs/test/compatibility:tests

* Run the test
> $ fx test f2fs-compatibility-test-v2
