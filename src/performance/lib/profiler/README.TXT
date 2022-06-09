This is a port of gperftools to the Fuchsia OS

An example of usage of the library is at garnet/examples/profiler

To try things out:

# Run the test app on the target device ...
fx shell run profiler_example

# Retrieve over the profiler trace
fx cp --to-host /tmp/profiler_example.ppf /tmp/profiler_example.ppf

# Install the modern go based version of pprof.
go get -u github.com/google/pprof

# Things to add to your .profile

# Where does the Go pprof execute reside?
export GO_HOME=${HOME}/go/bin/

# Where does pprof search for shared libraries and executables? (macos version, need to adjust for Linux)
export PPROF_BINARY_PATH=\
${FUCHSIA_DIR}/out/x64/exe.unstripped:\
${FUCHSIA_DIR}/out/x64/x64-shared/lib.unstripped:\
${FUCHSIA_DIR}/prebuilt/third_party/clang/mac-x64/lib/clang/8.0.0/x86_64-fuchsia/lib:\
${FUCHSIA_DIR}/out/x64/sdk/exported/zircon_sysroot/arch/x64/sysroot/debug

# Where is the tool chain? (macos version, need to adjust for Linux)
export PPROF_TOOLS=\
${FUCHSIA_DIR}/prebuilt/third_party/clang/mac-x64/bin

# It is easier to execute pprof from within ${FUCHSIA_DIR}/out/x64 (or whatever appropriate to your case)
cd ${FUCHSIA_DIR}/out/x64

# Execute pprof to extract the distributions and to collate and symbolize the data.
${GO_HOME}/pprof -text -lines -nodecount 35 -compact_labels -source_path ${FUCHSIA_DIR} -trim_path ../../ exe.unstripped/profiler_example_bin /tmp/profiler_example.ppf
Showing nodes accounting for 67.56s, 86.67% of 77.95s total
Dropped 256 nodes (cum <= 0.39s)
Showing top 35 nodes out of 57
      flat  flat%   sum%        cum   cum%
    21.20s 27.20% 27.20%     21.20s 27.20%  Trie::Map::store garnet/examples/profiler/trie.h:135 (partial-inline)
    18.40s 23.60% 50.80%     18.40s 23.60%  Trie::Map::retrieve garnet/examples/profiler/trie.h:149 (inline)
     6.05s  7.76% 58.56%      6.05s  7.76%  std::__2::basic_string::__is_long prebuilt/third_party/clang/mac-x64/lib/clang/8.0.0/include/c++/v1/string:1413 (inline)
     2.32s  2.98% 61.54%      2.32s  2.98%  radix_map_performance_print garnet/examples/profiler/words.cc:157
     2.08s  2.67% 64.21%      2.08s  2.67%  memcmp zircon/third_party/ulib/musl/src/string/memcmp.c:5
     1.49s  1.91% 66.12%      1.49s  1.91%  check_if_magic garnet/examples/profiler/magic_numbers.cc
     1.15s  1.48% 67.59%      1.20s  1.54%  Trie::Map::store garnet/examples/profiler/trie.h:131 (partial-inline)
     1.13s  1.45% 69.04%      1.13s  1.45%  Trie::Map::store garnet/examples/profiler/trie.h
     1.09s  1.40% 70.44%      1.14s  1.46%  Trie::Map::retrieve garnet/examples/profiler/trie.h:145 (inline)
     1.04s  1.33% 71.78%      1.04s  1.33%  Trie::Map::retrieve garnet/examples/profiler/trie.h:147 (inline)
     0.91s  1.17% 72.94%      6.69s  8.58%  std::__2::basic_string::__get_pointer prebuilt/third_party/clang/mac-x64/lib/clang/8.0.0/include/c++/v1/string:1507 (inline)
     0.88s  1.13% 74.07%      0.88s  1.13%  memcmp zircon/third_party/ulib/musl/src/string/memcmp.c:7
     0.86s  1.10% 75.18%      0.86s  1.10%  Trie::Map::retrieve garnet/examples/profiler/trie.h:148 (inline)
     0.75s  0.96% 76.14%      0.75s  0.96%  std::__2::basic_string::compare prebuilt/third_party/clang/mac-x64/lib/clang/8.0.0/include/c++/v1/string:3653 (inline)
     0.66s  0.85% 76.99%      0.66s  0.85%  generate_or_check garnet/examples/profiler/magic_numbers.cc:84
     0.65s  0.83% 77.82%      0.97s  1.24%  std::__2::__hash_table::__emplace_unique_key_args prebuilt/third_party/clang/mac-x64/lib/clang/8.0.0/include/c++/v1/__hash_table:2132
     0.63s  0.81% 78.63%      0.63s  0.81%  check_if_magic garnet/examples/profiler/magic_numbers.cc:49
     0.63s  0.81% 79.44%      0.63s  0.81%  std::__2::__hash_table::__emplace_unique_key_args prebuilt/third_party/clang/mac-x64/lib/clang/8.0.0/include/c++/v1/__hash_table:2131
     0.61s  0.78% 80.22%      0.61s  0.78%  atomic_store /b/s/w/ir/kitchen-workdir/llvm-project/compiler-rt/lib/sanitizer_common/sanitizer_atomic_clang_x86.h:82 (inline)
     0.57s  0.73% 80.95%      0.57s  0.73%  Trie::Set::store garnet/examples/profiler/trie.h:70
     0.54s  0.69% 81.64%      4.72s  6.06%  Trie::Map::store garnet/examples/profiler/trie.h:133 (partial-inline)
     0.52s  0.67% 82.31%      0.52s  0.67%  std::__2::char_traits::compare prebuilt/third_party/clang/mac-x64/lib/clang/8.0.0/include/c++/v1/__string:249 (inline)
     0.50s  0.64% 82.95%      0.50s  0.64%  generate_or_check garnet/examples/profiler/magic_numbers.cc:82
     0.50s  0.64% 83.59%      0.50s  0.64%  std::__2::__constrain_hash prebuilt/third_party/clang/mac-x64/lib/clang/8.0.0/include/c++/v1/__hash_table:128 (inline)
     0.50s  0.64% 84.23%      0.50s  0.64%  std::__2::__hash_table::__emplace_unique_key_args prebuilt/third_party/clang/mac-x64/lib/clang/8.0.0/include/c++/v1/__hash_table
     0.48s  0.62% 84.85%      0.48s  0.62%  Trie::Map::store garnet/examples/profiler/trie.h:140 (inline)
     0.48s  0.62% 85.47%      0.48s  0.62%  Trie::Map::~Map garnet/examples/profiler/trie.h:125
     0.44s  0.56% 86.03%      0.44s  0.56%  std::__2::__tree::__find_equal prebuilt/third_party/clang/mac-x64/lib/clang/8.0.0/include/c++/v1/__tree:2060 (inline)
     0.24s  0.31% 86.34%      2.81s  3.60%  std::__2::__tree::__find_equal prebuilt/third_party/clang/mac-x64/lib/clang/8.0.0/include/c++/v1/__tree:2048 (inline)
     0.20s  0.26% 86.59%      0.92s  1.18%  check_if_magic garnet/examples/profiler/magic_numbers.cc:48
     0.05s 0.064% 86.66%      0.49s  0.63%  Trie::Set::store garnet/examples/profiler/trie.h:68
     0.01s 0.013% 86.67%      7.87s 10.10%  radix_map_performance_print garnet/examples/profiler/words.cc:147
         0     0% 86.67%      0.49s  0.63%  __scudo::Allocator::allocate /b/s/w/ir/kitchen-workdir/llvm-project/compiler-rt/lib/scudo/scudo_allocator.cpp:377
         0     0% 86.67%     77.95s   100%  async_loop_dispatch_task zircon/system/ulib/async-loop/loop.c:398 (inline)
         0     0% 86.67%     77.95s   100%  async_loop_dispatch_tasks zircon/system/ulib/async-loop/loop.c:378

#Or for a more fun browser-based interactive version of the above:
${GO_HOME}/pprof -http=:8080 -source_path ${FUCHSIA_DIR} -trim_path ../../ exe.unstripped/profiler_example_bin /tmp/profiler_example.ppf
