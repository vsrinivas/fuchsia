# Symbolizer

This document outlines how to symbolize crashes and backtraces in Zircon.

## Overview

If you have some text that contains the required information, you can symbolize
that text by piping it into `./scripts/symbolize`. You can run
`./scripts/symbolize -h` to see how to use it.

If you use `loglistener` on x64, this is quite simple. You can simply pipe
directly into symbolize.

```
loglistener | ./scripts/symbolize
```

You can use the standard flags found in other scripts in Zircon to specify
other builds than build-x64. For instance, if you want to symbolize an arm64
build that was built with Clang you can run the following:

```
loglistener | ./scripts/symbolize -a arm64 -C
```

If you're familiar with what `ids.txt` is, and you know what you're doing you
can also specify `ids.txt` directly.

```
loglistener | ./scripts/symbolize build-arm64-asan/ids.txt
```

## ASan with QEMU Example

For a slightly more involved case we'll consider a complete workflow to compile
and symbolize an ASan crash.

First build (you can use `./scripts/build-zircon-x86 -A` as well):

```
make -j $JOBS USE_ASAN=true
```

Now you'll want to run this on QEMU, but if you just run it directly
you'll be stuck copy pasting the output into a file. You can use `tee`
to solve this:

```
./scripts/run-zircon-x86 -A | tee ~/log.txt
```

This will create a log.txt file in your home directory (feel free to place it
anywhere) that will contain all output from your QEMU run. While QEMU is
running, we'll need to create a crash. To get a handy ASan crash result we can
use `crasher`

```
$ crasher use_after_free
```

Now just pipe the output thought the symbolizer

```
./scripts/symbolize -A < ~/log.txt
```

# TODO(TC-283): Remove this after TC-283 is solved.
You might be wondering if you can just pipe QEMU directly into the symbolizer.
Right now this won't cause an error but it is not really usable because the
symbolizer buffers on a line by line basis. This may be possible in the future.
