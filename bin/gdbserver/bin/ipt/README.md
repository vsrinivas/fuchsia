# Intel Processor Tracing

Intel Processor Tracing is a h/w based instruction tracing capability
provided by newer Intel chips (skylake and onward). See Chapter 36 of
Intel Volume 3 for architectural details.

Tracing is controlled with the "ipt" program on the target.

Two modes of operation are supported: CPU and thread.
In CPU-based tracing, each CPU is traced regardless of what is executing
on the CPU. In thread-based tracing, only threads of a specified program
are traced (for now), the program and its arguments are specified in command
line arguments.

All forms of tracing generally have six steps:

- allocate trace resources (e.g., trace buffers)
- start
- stop
- collect trace output (e.g., write trace buffers to files)
- free trace resources (e.g., trace buffers)
- print results

The rest of this document goes into these steps for each of the modes.

N.B. Please don't try to collect and print a trace until you've read the
"Additional Requirements" section: The target must be booted with certain
kernel command line options.

## CPU-based Tracing

CPU-based tracing can be done in one of two ways: either by enabling
it for the duration of one program, or by starting/stopping tracing
separately from running one (or more) programs.

Note: If tracing is non-circular then each cpu will stop tracing when its
buffer becomes full.

### Tracing One Program

To time the starting and stopping of tracing around one program run it
as follows:

```
$ ipt [options] /path/to/program [program arguments ...]
```

This will:
- turn on tracing,
- run the program,
- turn off tracing and collect the traces after the program exits.

### Starting/Stopping Tracing Separately

CPU-based tracing can be controlled with the "--control"
option to the "ipt" program.

For flexibility "--control" takes several individual commands:

- init: allocate trace buffers and prepare for tracing
- start: start the trace
- stop: stop the trace:
- dump: write the trace buffers to disk
- reset: deallocate the trace buffers

CPU-based tracing is generally started with the following command:

$ ipt [options] --control init start

This, obviously, combines both initialization and starting of the trace,
but they don't have to be done together.

While tracing is enabled you can run whatever you like.

CPU-based tracing is generally stopped with the following command:

$ ipt --control stop dump reset

This command also writes the trace to files (with the dump command)
and then resets tracing, releasing all buffers (with the reset command).
This is for simplicity, but the steps don't have to be done together.

## Thread-based Tracing

Thread-based tracing is currently only supported by passing the program
to trace, and its arguments, to the "ipt" program. A new process is started
and tracing stops when the program exits.

This mode is not supported quite yet.

## Collecting trace output

The first step in printing a trace is copying the trace output files
from the zircon device to the development host: printing is currently
only supported on linux/macosx.

There are several output files:

- trace buffers (one for each cpu or thread)

These files have suffix ".pt" and there is one file for each cpu or thread,
depending upon the trace mode.

- ktrace output

Various kernel-level data is needed in order to understand the trace,
and IPT uses the "ktrace" facility to collect this data. This file has
suffix ".ktrace".

- trace output list

To simplify data collection trace output includes a file that lists
the files containing trace buffer data. There is one line for each
collected trace buffer. This file has suffix ".ptlist".

At present printing of traces is only supported on linux/mac development
hosts. A script is provided to copy the needed files from the target
to the host:

```
$ sh bin/gdbserver/bin/ipt-dump/ipt-copy-ptout.sh \
  <zircon-hostname> <input-path-prefix> <output-path-prefix>
```

Example:

```
$ sh bin/gdbserver/bin/ipt-dump/ipt-copy-ptout.sh \
  "" /tmp/ptout ./ptout
```

This will copy files from the default zircon host ("") with prefix
"/tmp/ptout" on the target and write them locally with prefix "./ptout".

Example session:

```
zircon$ ipt --num-buffers=256 --config="'cyc;cyc-thresh=2'" \
  /system/test/debugserver/syscall-test 1000
zircon$ ls -l /tmp/ptout.*
-rw-------    1 0        0            621984 Jul 11 13:08 ptout.cpu0.pt
-rw-------    1 0        0             10080 Jul 11 13:08 ptout.cpu1.pt
-rw-------    1 0        0            259440 Jul 11 13:08 ptout.cpu2.pt
-rw-------    1 0        0            163664 Jul 11 13:08 ptout.cpu3.pt
-rw-------    1 0        0              5472 Jul 11 13:08 ptout.ktrace
-rw-------    1 0        0                84 Jul 11 13:08 ptout.ptlist

linux$ sh bin/gdbserver/bin/ipt-dump/ipt-copy-ptout.sh "" /tmp/ptout ./ptout
linux$ ls -l ptout.*
-rw-r----- 1 dje eng   621984 Jul 10 22:16 ptout.cpu0.pt
-rw-r----- 1 dje eng    10080 Jul 10 22:16 ptout.cpu1.pt
-rw-r----- 1 dje eng   259440 Jul 10 22:16 ptout.cpu2.pt
-rw-r----- 1 dje eng   163664 Jul 10 22:16 ptout.cpu3.pt
-rw-r----- 1 dje eng     5472 Jul 10 22:16 ptout.ktrace
-rw-r----- 1 dje eng       84 Jul 10 22:16 ptout.ptlist
-rw-r----- 1 dje eng       72 Jul 10 22:16 ptout.xptlist
```

The `--config` option is described below.

The "ptout.xptlist" file is a copy of "ptout.ptlist" with the paths
updated for their location on the host.

Note: You may see a "ptout.cpuid" file on the target.
This file is no longer needed.

## Printing results

At the moment printing of trace data must be done from either a linux
or mac development host. Two output formats are currently supported:
raw and calls. The "raw" format prints a trace of each instruction
executed. The "calls" format prints a trace of function calls
formatted to show subroutine call and return.

There are a lot of inputs in order to get usable output.
The "ipt-dump" program currently requires one to specify them all.

To see a list of all options run "ipt-dump --help".

Note: An early development version of "ipt-dump" included a disassembler
which was handy. However, the disassembler used wasn't the LLVM disassembler
and so was never checked in. It's still a work-in-progress to add this feature.

### Raw Output

To obtain a dump of raw output:

```
linux$ ZIRCON_BUILDROOT=out/build-zircon/build-zircon-pc-x86-64
linux$ FUCHSIA_BUILDROOT=out/debug-x86-64
linux$ $FUCHSIA_BUILDROOT/host_x64/ipt-dump \
  --ktrace=ptout.ktrace \
  --pt-list=ptout.xptlist \
  --map=loglistener.log \
  --ids=$ZIRCON_BUILDROOT/ids.txt \
  --ids=$FUCHSIA_BUILDROOT/ids.txt \
  --kernel=$ZIRCON_BUILDROOT/zircon.elf \
  --output-format=raw \
  --output-file=ptout.raw
```

A sample of the output:

```
Current function is now /usr/local/google/home/dje/fnl/ipt/fuchsia/out/debug-x86-64/exe.unstripped/syscall-test:main
227640018981: 608ffd59603c: other
227640018981: 608ffd59603e: other
227640018981: 608ffd596040: cjump
227640018981: 608ffd596042: other
227640018981: 608ffd596050: call
Entering unknown function
227640018981: 608ffd596120: jump
Current function is now /usr/local/google/home/dje/fnl/ipt/fuchsia/out/build-zircon/build-zircon-pc-x86-64/system/ulib/zircon/libzircon.so:VDSO_zx_syscall_test_0
227640019074: 5d162fcd9e3a: other
227640019074: 5d162fcd9e3c: other
227640019074: 5d162fcd9e3e: other
227640019074: 5d162fcd9e41: other
227640019074: 5d162fcd9e46: fcall
Space is now kernel
Current function is now ../out/build-zircon/build-zircon-pc-x86-64/zircon.elf:x86_syscall
227640019105: ffffffff80114c7f: other
227640019105: ffffffff80114c82: other
227640019105: ffffffff80114c8b: other
227640019105: ffffffff80114c94: other
227640019105: ffffffff80114c9c: other
227640019105: ffffffff80114c9e: other
227640019105: ffffffff80114c9f: other
227640019105: ffffffff80114ca3: cjump
227640019238: ffffffff80114ca5: jump
```

The first column is the TSC value, and the second column is the PC address.

TSC values are only recorded at control transfer instructions (branches, etc.)
and even then only as frequently as requested. That is why "other" instructions
(non-branches) show an unchanging TSC value.

Note: The output looks way cooler with the disassembly. :-) In time.

### Calls Output

To obtain a dump of calls output:

```
linux$ ZIRCON_BUILDROOT=out/build-zircon/build-zircon-pc-x86-64
linux$ FUCHSIA_BUILDROOT=out/debug-x86-64
linux$ $FUCHSIA_BUILDROOT/host_x64/ipt-dump \
  --ktrace=ptout.ktrace \
  --pt-list=ptout.xptlist \
  --map=loglistener.log \
  --ids=$ZIRCON_BUILDROOT/ids.txt \
  --ids=$FUCHSIA_BUILDROOT/ids.txt \
  --kernel=$ZIRCON_BUILDROOT/zircon.elf \
  --output-format=calls \
  --output-file=ptout.calls
```

A sample of the output:

```
[ 5288094]                         [+   5] U call    main+64 -> 608ffd596120
[ 5288095] 5889746   [93]          [+   1] U jump        101b4a000:608ffd596120
[ 5288100]                         [+   5] U fcall       VDSO_zx_syscall_test_0+12 -> x86_syscall
[ 5288101] 5889777   [31]          [+   1] K other           x86_syscall
[ 5288109] 5889910   [133]         [+   8] K jump            x86_syscall+38
[ 5288110] 5891330   [1420]        [+   1] K other           101b4a000:ffffffff80115579
[ 5288112]                         [+   2] K call            101b4a000:ffffffff8011557e -> wrapper_syscall_test_0
[ 5288122]                         [+  10] K call                wrapper_syscall_test_0+35 -> ktrace_tiny
[ 5288128] 5891653   [323]         [+   6] K other                   ktrace_tiny+14
[ 5288129]                         [+   1] K return                  ktrace_tiny+15
[ 5288149]                         [+  20] K call                wrapper_syscall_test_0+190 -> _Z18sys_syscall_test_0v
[ 5288154]                         [+   5] K return                  _Z18sys_syscall_test_0v+7
[ 5288162]                         [+   8] K call                wrapper_syscall_test_0+156 -> ktrace_tiny
[ 5288168] 5891664   [11]          [+   6] K other                   ktrace_tiny+14
[ 5288169]                         [+   1] K return                  ktrace_tiny+15
[ 5288179]                         [+  10] K return              wrapper_syscall_test_0+189
[ 5288180] 5891675   [11]          [+   1] K other           101b4a000:ffffffff80115583
[ 5288191] 5891738   [63]          [+  11] K other           x86_syscall+85
[ 5288194]                         [+   3] K freturn         x86_syscall+90
[ 5288195] 5891741   [3]           [+   1] U other       VDSO_zx_syscall_test_0+14
[ 5288197]                         [+   2] U return      VDSO_zx_syscall_test_0+18
[ 5288198] 5891746   [5]           [+   1] U other   main+69
```

## Trace Options

Various attributes of the trace may be specified when the trace is
initialized:

- output file name
- buffer size
- stop-when-full or circular buffers
- trace only process with specific CR3 value
- whether to trace kernel or user space (or both)
- timing accuracy

A list of all options may be obtained with the "--help" option.

### Output File Name

There are several trace output files:

- data files, one file per cpu or thread (suffix ".pt")
- ktrace data (suffix ".ktrace")
- list of data files (suffix ".ptlist")

The name of the output files may be specified with the
"--output-path-prefix" option. The default is "/tmp/ptout".

### Buffer Size

Trace buffers are composed of a collection of smaller buffers to make
one large buffer. Buffers are accessed by h/w with their physical address,
therefore each sub-buffer must consist of contiguous pages. However,
allocating large buffers from contiguous pages is problematic: after the
system has been running awhile it's not possible. To cope with this the
h/w lets one compose one large buffer out of several smaller buffers.

The size of the trace buffer for each cpu/thread is specified with two
parameters.

- "--buffer-order=N" specifies the size of each sub-buffer, as the number of
  pages specified by a power of 2
- "--num-buffers=N" specifies the number of component buffers that are combined
  to make the large "virtual" trace buffer

The default is --buffer-order=2 --num-buffers=16, which means 16 sets of
16 KB buffers totalling 256KB.

### Stop-when-full or Circular Buffers

IPT can either be configured to stop tracing when a buffer is full
or treat the buffer as circular. The default is non-circular.
To enable circular buffers pass "--circular".

## Additional Requirements

Several additional pieces must be in place to be able to correctly
read trace output:

- process cr3 data
- mapping of build ids to unstripped ELF files
- data for loaded shared libraries

### Process CR3 data

Individual processes, as well as the kernel, are distinguished by their
CR3 value. This data is collected by the kernel and (currently) included in
ktrace output. Therefore the required elements in ktrace output must be
enabled. These all live in the "arch" ktrace group.

```
#define KTRACE_GRP_ARCH 0x080
```

Since the ktrace buffer is circular and fixed in size it is recommended
to limit the amount of data recorded in it.
The author generally always includes the following on the kernel command line:

```
ldso.trace=true ktrace.grpmask=0x80
```

### Build ID -> ELF map

The Fuchsia build system creates files named "ids.txt" that contain
the mapping from build ids to ELF files. Intel Processor Trace requires
post-trace access to the binary that was running.
[This means, for example, that tracing of self-modifying or dynamically
generated code is not currently supported.]

Paths to these files must be provided via the "--ids=PATH" option.
Generallly there are two. See the above examples.

Note that it is *critical* that the contents of the files match what
was running on the target when the trace was generated. Printing a trace
after you change something and recompile generally doesn't work (depending
of course on what binaries got changed).

### Data For Loaded Shared Libraries

Part of what's necessary in order to find the ELF files for running
code is a list of the loaded shared libraries, their build ids, and
where they were loaded. This is done by passing the following
on the kernel command line:

```
ldso.trace=true
```

Also, one must save "loglistener" output to a file and pass the
PATH of this file to "ipt-dump". The author runs loglistener thusly:

```
$ TOOLSDIR="out/build-zircon/build-zircon-pc-x86-64/tools"
$ $TOOLSDIR/loglistener 2>&1 | tee loglistener.log
```

The path to the log file is passed with the "--map=PATH" option.
Think of the needed data as being the "load map".

Ipt-dump is smart enough to recognize reboots in the loglistener output
and discard all preceding trace data so there is no need to restart
loglistener for each reboot.
