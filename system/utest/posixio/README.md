# POSIX IO Tests

This directory contains API coverage tests of POSIX IO. This is
loosely defined as the set of functions that mxio implements. The goal
is to test the error cases of each of these functions. It is not a
test of the underlying RIO transport or backing filesystems.

Note that POSIX stipulates that "if more than one error occurs in
processing a function call, any one of the possible errors may be
returned, as the order of detection is undefined." The tests in this
directory test our implementation of POSIX, and not the spec
itself. For example, we check that

    openat(not_a_directory, "", O_RDONLY)

returns `ENOENT`, rather than it returning one of `ENOENT` or
`ENOTDIR`, both of which could apply.
