# Syscall definitions

This is a work-in-progress definition of syscalls as FIDL.

**This is not yet used by the build! Do not worry about updating it!**

The location you're looking for is currently `//zircon/system/public/zircon/syscalls.banjo`.


## Notes for interested in working on the transition

- Named xyz.test.fidl for now only to not require API review on gerrit, and to
  make it slightly more clear that they're not the real definitions yet.
- See tools/kazoo for the processor of these, and in particular the in-progress
  tests that compare the output of fidlc+kazoo to the current output of
  banjo+abigen.
