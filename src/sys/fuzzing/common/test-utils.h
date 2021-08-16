// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_TEST_UTILS_H_
#define SRC_SYS_FUZZING_COMMON_TEST_UTILS_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/sync/completion.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/zx/eventpair.h>
#include <stddef.h>
#include <stdint.h>

#include <atomic>
#include <limits>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/synchronization/thread_annotations.h"

namespace fuzzing {

// Helper function to create a deterministically pseudorandom integer type.
template <typename T>
T Pick() {
  static std::mt19937_64 prng;
  return static_cast<T>(prng() & std::numeric_limits<T>::max());
}

// Helper function to create an array of deterministically pseudorandom integer types.
template <typename T>
void PickArray(T* out, size_t out_len) {
  for (size_t i = 0; i < out_len; ++i) {
    out[i] = Pick<T>();
  }
}

// Helper function to create a vector of deterministically pseudorandom integer types.
template <typename T = uint8_t>
std::vector<T> PickVector(size_t size) {
  std::vector<T> v;
  v.reserve(size);
  for (size_t i = 0; i < size; ++i) {
    v.push_back(Pick<T>());
  }
  return v;
}

// Common base class for unit tests. This class provides methods for synchrnoizing different threads
// and FIDL services and for recording and retrieving information from the global, exported
// interface functions.
class TestBase : public ::testing::Test {
 public:
  ~TestBase() override = default;

  // These methods can be called from the library interface functions outside the test instance.
  // If |Record| is called multiple times, or |Set*| is called multiple times with the same |key|,
  // only the data from the last call will be saved.
  // See also the |FUZZER_TEST_*| macros below.
  static TestBase* Record(const std::string& func);
  void Signal();
  void SetU64(const std::string& key, uint64_t val) FXL_LOCKS_EXCLUDED(mutex_);
  void SetBytes(const std::string& key, const uint8_t* buf, size_t buf_len)
      FXL_LOCKS_EXCLUDED(mutex_);
  void SetPaired(zx::eventpair ep) { ep_ = std::move(ep); }

 protected:
  TestBase();

  // gTest methods.
  void SetUp() override;
  void TearDown() override;

  // FIDL methods.
  async_dispatcher_t* dispatcher() const { return loop_.dispatcher(); }
  std::unique_ptr<sys::ComponentContext> TakeContext() { return std::move(context_); }

  template <typename Interface>
  void AddPublicService(fidl::InterfaceRequestHandler<Interface> handler) {
    ASSERT_NE(context_.get(), nullptr);
    context_->outgoing()->AddPublicService(std::move(handler));
  }

  template <typename Interface>
  void ConnectToPublicService(fidl::InterfaceRequest<Interface> request) {
    provider_.ConnectToPublicService(std::move(request));
  }

  // These methods correspond to those public methods called by the library interface functions.
  // See also the |FUZZER_TEST_*| macros below.
  const char* GetRecorded() FXL_LOCKS_EXCLUDED(mutex_);
  void Wait();
  uint64_t GetU64(const std::string& key) FXL_LOCKS_EXCLUDED(mutex_);
  std::vector<uint8_t> GetBytes(const std::string& key) FXL_LOCKS_EXCLUDED(mutex_);
  void MatchBytes(const std::string& key, const std::vector<uint8_t>& bytes)
      FXL_LOCKS_EXCLUDED(mutex_);
  void SignalPeer(zx_signals_t signals);
  zx_signals_t WaitOne();

 private:
  // Macro variables.
  static std::atomic<TestBase*> current_;

  // FIDL variables.
  async::Loop loop_;
  sys::testing::ComponentContextProvider provider_;
  std::unique_ptr<sys::ComponentContext> context_;

  // Sync variables.
  sync_completion_t sync_;

  // Eventpair variables.
  zx::eventpair ep_;

  // Recording variables.
  std::mutex mutex_;
  std::string recorded_ FXL_GUARDED_BY(mutex_);
  std::unordered_map<std::string, uint64_t> u64s_ FXL_GUARDED_BY(mutex_);
  std::unordered_map<std::string, std::vector<uint8_t>> bytes_ FXL_GUARDED_BY(mutex_);

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(TestBase);
};

// These macros allow the exported, global interface functions to interact with the unit tests.
#define FUZZER_TEST_RECORD_U64(x) \
  TestBase::Record(__FUNCTION__)->SetU64(#x, static_cast<uint64_t>(x))
#define FUZZER_TEST_RECORD_BYTES(o, o_len) TestBase::Record(__FUNCTION__)->SetBytes(#o, o, o_len)
#define FUZZER_TEST_SIGNAL() TestBase::Record(__FUNCTION__)->Signal()

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_TEST_UTILS_H_
