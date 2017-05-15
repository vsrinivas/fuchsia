# Fuzzing the fidl host tools

Some notes on fuzzing the `system/host/fidl` parser using [afl-fuzz](http://lcamtuf.coredump.cx/afl/).

## Build afl-fuzz

Download and build it, then:
```
export AFL_PATH=~/src/afl-2.41b/
```
with whatever path you downloaded and built it with.

## Patch the parser to not trap on invalid syntax

afl-fuzz treats crashes as interesting but the parser currently calls `__builtin_trap()` when it encounters invalid
syntax. Remove that line in [parser.h](../system/host/fidl/parser.h) - its in the `Parser::Fail()` method.

## Build the `fidl` tool with afl-fuzz's instrumentation

Clear any existing build and then build with the afl-fuzz compiler wrappers.

```
cd $MAGENTA_DIR
rm -fr build-magenta-pc-x86-64
PATH=$PWD/prebuilt/downloads/clang+llvm-x86_64-linux/bin/:$PATH:$AFL_PATH make \
  build-magenta-pc-x86-64/tools/fidl HOST_TOOLCHAIN_PREFIX=afl-
```
adjusting if you're not building on x86-64 Linux, etc.

## Run the fuzzer

The parser includes some examples to use as inputs. As fidl2 becomes adopted we can expand our inputs to include all of
the different interfaces declared across our tree, but for now we use what's in `system/host/fidl/examples`.

```
$AFL_PATH/afl-fuzz -i system/host/fidl/examples -o fidl-fuzz-out build-magenta-pc-x86-64/tools/fidl dump '@@'
```

## Results

Running against the source from early May 2017, there were no crashes or hangs after two days of fuzzing on a fairly
fast machine. It ran over 300 million executions.
