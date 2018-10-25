# On target device ...
/pkgfs/system/bin/profiler_unittest 100 4 /tmp/abc.txt

# On host system ... pull over the profiler data file.
fx cp --to-host /tmp/profiledata_unittest.tmp /tmp/profiledata_unittest.tmp

# Install the modern go based version of pprof.
go get -u github.com/google/pprof

# Where is your Fuchsia workspace? The build should be in ${FX}/out/...
export FX=${HOME}/fuchsia

# Where does the Go pprof execute reside?
export GO_HOME=${HOME}/go/bin/

# Where does pprof search for shared libraries and executables?
export PPROF_BINARY_PATH=${FX}/out/x64/exe.unstripped:${FX}/buildtools/mac-x64/clang/lib/clang/8.0.0/x86_64-fuchsia/lib:${FX}/out/x64/sdk/exported/zircon_sysroot/arch/x64/sysroot/debug/:${FX}/out/x64/x64-shared/lib.unstripped:${FX}/out/x64/exe.unstripped

# Where is the tool chain?
export CLANG_BIN_PATH=${FX}/buildtools/mac-x64/clang/bin/
export CLANG_BIN_PATH=${FX}/buildtools/linux-x64/clang/bin/

# Execute pprof to extract the distributions and to collate and symbolize the data.
PATH=$PATH:${CLANG_BIN_PATH} ${GO_HOME}/pprof -lines -text /tmp/profiledata_unittest.tmp

You should obtain something like this:

File: profiler_unittest
Type: cpu
Showing nodes accounting for 1.43s, 100% of 1.43s total
      flat  flat%   sum%        cum   cum%
     0.90s 62.94% 62.94%      0.90s 62.94%  SYSCALL_zx_futex_wait /Users/fricc/fuchsia/out/build-zircon/build-x64/gen/global/include/zircon/syscalls-x86-64.S:58
     0.11s  7.69% 70.63%      0.11s  7.69%  test_other_thread ../../third_party/gperftools/src/tests/profiler_unittest.cc:63
     0.09s  6.29% 76.92%      0.09s  6.29%  test_other_thread ../../third_party/gperftools/src/tests/profiler_unittest.cc:62
     0.07s  4.90% 81.82%      0.07s  4.90%  __memcpy_fwd ../../zircon/third_party/ulib/musl/src/string/x86_64/memcpy.S:15
     0.05s  3.50% 85.31%      0.05s  3.50%  test_main_thread ../../third_party/gperftools/src/tests/profiler_unittest.cc:76 (inline)
     0.04s  2.80% 88.11%      0.04s  2.80%  vsnprintf ../../zircon/third_party/ulib/musl/src/stdio/vsnprintf.c:20
     0.03s  2.10% 90.21%      0.03s  2.10%  __fwritex ../../zircon/third_party/ulib/musl/src/stdio/fwrite.c:27
     0.02s  1.40% 91.61%      0.02s  1.40%  __fwritex ../../zircon/third_party/ulib/musl/src/stdio/fwrite.c:4
     0.02s  1.40% 93.01%      0.02s  1.40%  test_main_thread ../../third_party/gperftools/src/tests/profiler_unittest.cc:75 (inline)
     0.01s   0.7% 93.71%      0.01s   0.7%  __fwritex ../../zircon/third_party/ulib/musl/src/stdio/fwrite.c:13
     0.01s   0.7% 94.41%      0.01s   0.7%  _zx_thread_start ??
     0.01s   0.7% 95.10%      0.01s   0.7%  fmt_u ../../zircon/third_party/ulib/musl/src/stdio/vfprintf.c:722 (inline)
     0.01s   0.7% 95.80%      0.01s   0.7%  pad.part.1 ../../zircon/third_party/ulib/musl/src/stdio/vfprintf.c:211
     0.01s   0.7% 96.50%      0.01s   0.7%  pop_arg.part.0 ../../zircon/third_party/ulib/musl/src/stdio/vfprintf.c:148
     0.01s   0.7% 97.20%      0.01s   0.7%  printf_core ../../zircon/third_party/ulib/musl/src/stdio/vfprintf.c:210
     0.01s   0.7% 97.90%      0.01s   0.7%  printf_core ../../zircon/third_party/ulib/musl/src/stdio/vfprintf.c:542
     0.01s   0.7% 98.60%      0.01s   0.7%  printf_core ../../zircon/third_party/ulib/musl/src/stdio/vfprintf.c:581
     0.01s   0.7% 99.30%      0.01s   0.7%  printf_core ../../zircon/third_party/ulib/musl/src/stdio/vfprintf.c:715
     0.01s   0.7%   100%      0.01s   0.7%  test_other_thread ../../third_party/gperftools/src/tests/profiler_unittest.cc:61
         0     0%   100%      1.43s   100%  async::internal::RetainedTask::Handler ../../zircon/system/ulib/async/task.cpp:21
         0     0%   100%      1.43s   100%  async_loop_dispatch_task ../../zircon/system/ulib/async-loop/loop.c:398 (inline)
         0     0%   100%      1.43s   100%  async_loop_dispatch_tasks ../../zircon/system/ulib/async-loop/loop.c:378 (inline)
         0     0%   100%      1.43s   100%  async_loop_run ../../zircon/system/ulib/async-loop/loop.c:240
         0     0%   100%      1.43s   100%  async_loop_run_once ../../zircon/system/ulib/async-loop/loop.c:274 (inline)
         0     0%   100%      1.43s   100%  async_loop_run_thread ../../zircon/system/ulib/async-loop/loop.c:749
         0     0%   100%      1.43s   100%  fbl::internal::Function::operator() ../../zircon/system/ulib/fbl/include/fbl/function.h:238 (inline)
         0     0%   100%      0.04s  2.80%  main ../../third_party/gperftools/src/tests/profiler_unittest.cc:109
         0     0%   100%      0.03s  2.10%  main ../../third_party/gperftools/src/tests/profiler_unittest.cc:140
         0     0%   100%      0.01s   0.7%  printf_core ../../zircon/third_party/ulib/musl/src/stdio/vfprintf.c:722
         0     0%   100%      1.43s   100%  start_c11 ../../zircon/third_party/ulib/musl/pthread/pthread_create.c:33
         0     0%   100%      1.43s   100%  thread_trampoline ../../zircon/system/ulib/runtime/thread.c:94
