# Leak detector library

## Description
This library implements a crud memory leak detection mechanism using ASAN.

Both ASAN and MSAN have better mechanisms, but none of these are currently
available on Fuchsia. When any of these becomes available, this library should
be removed from the repository.

This library works by keeping track of allocation and deallocation. Once a given
number of allocations that have not been released has been made, it finds the
stack that is responsible for the most unreleased allocations and use ASAN to
stop the program and show the given stack.

To use this library, one must:
1. Add //src/ledger/lib/detect\_leak as a dependency of the binary that has a
   leak.
2. Adjust kKeepAlloc in //src/ledger/lib/detect\_leak/detect\_leak.cc: one wants
   this to be big enough that the stack with the most unreleased allocations
   will be a leak, but increasing it will also increase the time the binary
   needs to run before returning a result.
3. Run the binary under ASAN and wait for it to crash. When this happens, ASAN
   will display a diagnostic about a double free error. The release stacks are
   irrelevant. The allocation stack will be the stack that had the most
   unreleased allocations at the time of the crash.
