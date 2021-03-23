// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FUZZING_FIDL_RUNTIME_INTERFACE_H_
#define SRC_LIB_FUZZING_FIDL_RUNTIME_INTERFACE_H_

#include <stddef.h>
#include <stdint.h>
#include <zircon/compiler.h>

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

//////////////////////////////////////////////////
// FuzzerProxy interface.
// See also:
//   compiler-rt's lib/fuzzer/FuzzerRemoteInterface.h
//   remote.cc

// These symbols are the "proxy" side of libFuzzer's remote interface and are implemented by the
// fuzzer engine. They are ALSO implemented by the "remote" FIDL fuzzing library; these
// implementations simply forward the call to the fuzzer engine.
//
// Note: clang-tidy doesn't like `unsigned long` or symbols like `__*`, but they need to match the
// LLVM-defined interfaces. These lines have been marked NOLINT.

__EXPORT void FuzzerAcceptRemotes();
__EXPORT void FuzzerShutdownRemotes();

// NOLINTNEXTLINE(google-runtime-int)
__EXPORT void FuzzerProxyConnect(unsigned long pid, void* options, size_t options_len);

// NOLINTNEXTLINE(google-runtime-int)
__EXPORT uintptr_t FuzzerProxyAddCoverage(unsigned long pid, uint8_t* counters_begin,
                                          uint8_t* counters_end, const uintptr_t* pcs_begin,
                                          const uintptr_t* pcs_end);

// NOLINTNEXTLINE(google-runtime-int)
__EXPORT void FuzzerProxyExecutionStarted(unsigned long pid);

// NOLINTNEXTLINE(google-runtime-int)
__EXPORT void FuzzerProxyExecutionFinished(unsigned long pid, int leak_likely);

// NOLINTNEXTLINE(google-runtime-int)
__EXPORT void FuzzerProxyDisconnect(unsigned long pid);

//////////////////////////////////////////////////
// FuzzerMonitor interface.
// See also:
//   compiler-rt's lib/fuzzer/FuzzerMonitor.h
//   remote.cc

// These symbols are the "proxy" side of libFuzzer's monitoring interface and are implemented by the
// fuzzer engine. They are ALSO implemented by the "remote" FIDL fuzzing library; these
// implementations simply forward the call to the fuzzer engine.

// NOLINTNEXTLINE(google-runtime-int)
__EXPORT void FuzzerCrashSignalCallback(unsigned long pid);

__EXPORT void FuzzerDeathCallback();

// NOLINTNEXTLINE(google-runtime-int)
__EXPORT void FuzzerExitCallback(unsigned long pid);

// NOLINTNEXTLINE(google-runtime-int)
__EXPORT void FuzzerLeakCallback(unsigned long pid);

// NOLINTNEXTLINE(google-runtime-int)
__EXPORT void FuzzerMallocLimitCallback(unsigned long pid, size_t size);

// NOLINTNEXTLINE(google-runtime-int)
__EXPORT void FuzzerRssLimitCallback(unsigned long pid);

//////////////////////////////////////////////////
// FuzzerRemote interface.
// See also:
//   compiler-rt's lib/fuzzer/FuzzerRemoteInterface.h
//   remote.cc

// These symbols are the "remote" side of libFuzzer's remote interface and are implemented by the
// fuzzer remote library. They are ALSO implemented by the "proxy" FIDL fuzzing library; these
// implementations simply forward the call to the remote fuzzing process.

// NOLINTNEXTLINE(google-runtime-int)
__EXPORT void FuzzerRemoteStartExecution(unsigned long pid, uint32_t exec_options);

// NOLINTNEXTLINE(google-runtime-int)
__EXPORT void FuzzerRemoteFinishExecution(unsigned long pid);

// NOLINTNEXTLINE(google-runtime-int)
__EXPORT void FuzzerRemotePrintPC(unsigned long pid, const char* symbolized_fmt,
                                  const char* fallback_fmt, uintptr_t pc);

// NOLINTNEXTLINE(google-runtime-int)
__EXPORT void FuzzerRemoteDescribePC(unsigned long pid, const char* symbolized_fmt, uintptr_t pc,
                                     char* desc, size_t desc_len);

// NOLINTNEXTLINE(google-runtime-int)
__EXPORT void FuzzerRemotePrintStackTrace(unsigned long pid);

// NOLINTNEXTLINE(google-runtime-int)
__EXPORT void FuzzerRemotePrintMemoryProfile(unsigned long pid);

// NOLINTNEXTLINE(google-runtime-int)
__EXPORT void FuzzerRemoteDetectLeaksAtExit(unsigned long pid);

//////////////////////////////////////////////////
// sanitizer_common exports.
// See also:
//   compiler-rt's include/sanitizer/common_interface_defs.h

// Symbolization function provided by compiler-rt's lib/sanitizer_common. Since all symbolization on
// Fuchsia is done offline, the fuzzer proxy library invokes this directly instead of requesting
// that the remote process symbolize a PC referring to its address space.

// NOLINTNEXTLINE(bugprone-reserved-identifier)
__WEAK __EXPORT void __sanitizer_symbolize_pc(void*, const char* fmt, char* out_buf,
                                              size_t out_buf_size);

#ifdef __cplusplus
}  // extern "C"

#include <limits>

// C++ constants corresponding to those in compiler-rt's lib/fuzzer/FuzzerRemoteInterface.h
namespace fuzzing {
constexpr uintptr_t kInvalidIdx = std::numeric_limits<uintptr_t>::max();
constexpr uint32_t kLeakDetection = 1 << 0;
}  // namespace fuzzing

#endif  // __cplusplus
#endif  // SRC_LIB_FUZZING_FIDL_RUNTIME_INTERFACE_H_
