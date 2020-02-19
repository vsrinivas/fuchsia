# Debug Agent Test Data

This directory contains all test applications and libraries developed for testing and debugging
the debug agent. This means utilities for things like spawning processes, keeping threads looping
and having a .so that is also linked to generated binary.

Every new functionality that is not meant to be consumed by the end users but rather by the
developers of zxdb should put that code in here.

## Instructions

There are some constructs in this directory:

- Debug .so: A shared library that gets packaged that can be dynamically loaded through the
  "package" routing (/pkg/lib if started as a component). It's mostly loaded by the breakpoint tests
  to have a shared module with the binary they're debugging.
- Test binaries: These are actual binaries that get breakpoint tests run as a sub-process so that
  they can load breakpoints on them.
  See src/developer/debug/debug_agent/integration_tests/breakpoint_test.cc for an example.
- Test utilities: These are various one-off programs that are meant to be run manually, either as a
  shell program ($ /pkgfs/packages/debug_agent_tests/0/bin/program) or as a component
  ($ run this_component.cmx... See limbo caller as an example, as you do need the .cmx setup).

## Deploying

This suite of programs and helpers are mostly packed as part of the "debug_agent_tests" package,
which is defined in src/developer/debug/debug_agent/BUILD.gn. That package does all the job of
adding the correct dependencies and doing all the packaging/meta files managing.

### How to add a new test executable

The process to add a new executable is to:

1. Add a new executble to src/developer/debug/debug_agent/test_data/BUILD.gn
2. Add it as a dependency to the `:helpers_executable` group within the test_data BUILD.gn.
3. In the debug agent BUILD.gn (src/developer/debug/debug_agent/BUILD.gn), you need to specify that
   the `debug_agent_tests` package exports this new executable. For that, look for the `binaries`
   array and add the exported name of the executable in (1) and add it there.
4. If there is a .cmx file (meant to be run as a component), remember to also add the meta file
   translation in there. See `limbo_caller` or `test_suite` as an example of a test executable that
   also can get called as a component.

## Test Suite

The "Debug Agent Test Suite" is a more elaborate program that is used to create more complicated
scenerios, such as adding several watchpoints on another process, send multiple channel calls, etc.

To run you first must push the debug_agent_tests package:

```
fx build-push debug_agent_tests
```

With that, the package will be present at `/pkgfs/packages/debug_agent_tests/0`. The easies way to
run the test suite is to call it as a component:

```
$ run debug_agent_test_suite.cmx
```

The suite has CLI instructions about to execute the different test cases. Each test case is
documented so you can refer to the source to see what each test case is supposed to do.

The test suite has a set of helpers (test_suite_helpers.h) that facilitate the creation of test
cases (hence the name test suite). Normally if more intricate examples need to be written to try out
a behaviour, it's very probable that adding it to the test suite is the easiest way to go about it
developing and deploying it, as adding a new test case is very easy (see the last part of
test_suite.cc).

The test suite can be run directly (/pkgfs/packages/debug_agent_tests/0/bin/test_suite) or as a
component (run debug_agent_test_suite.cmx).



