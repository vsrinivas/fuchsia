// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/trace/event.h"

#include <zircon/process.h>
#include <zx/vmo.h>

#include "gtest/gtest.h"
#include "lib/fsl/tasks/message_loop.h"

namespace tracing {
namespace writer {
namespace {

struct EventTest : public ::testing::Test {
  EventTest() {
    zx::vmo buffer;
    zx::eventpair fence;
    assert(ZX_OK == zx::vmo::create(100000, 0u, &buffer));
    assert(ZX_OK == zx::eventpair::create(0u, &fence, &control_));
    StartTracing(std::move(buffer), std::move(fence), {"cat"},
                 [this](TraceDisposition disposition) { loop_.QuitNow(); });
  }

  ~EventTest() {
    StopTracing();
    loop_.Run();
  }

 private:
  fsl::MessageLoop loop_;
  zx::eventpair control_;
};

TEST_F(EventTest, Koid) {
  auto koid = TRACE_KOID(42u);
  EXPECT_EQ(42u, koid.value);
}

TEST_F(EventTest, Enabled) {
  EXPECT_EQ(true, TRACE_ENABLED());

  StopTracing();
  EXPECT_EQ(false, TRACE_ENABLED());
}

TEST_F(EventTest, CategoryEnabled) {
  EXPECT_EQ(true, TRACE_CATEGORY_ENABLED("cat"));
  EXPECT_EQ(false, TRACE_CATEGORY_ENABLED("disabled"));

  StopTracing();
  EXPECT_EQ(false, TRACE_CATEGORY_ENABLED("cat"));
  EXPECT_EQ(false, TRACE_CATEGORY_ENABLED("disabled"));
}

TEST_F(EventTest, TraceNounce) {
  const size_t nonces_count = 100;
  std::set<uint64_t> nonces;
  for (size_t i = 0; i < nonces_count; ++i) {
    nonces.insert(TRACE_NONCE());
  }
  EXPECT_EQ(nonces_count, nonces.size());
  EXPECT_EQ(nonces.end(), nonces.find(0u));
}

TEST_F(EventTest, Instant) {
  TRACE_INSTANT("cat", "name", TRACE_SCOPE_GLOBAL);
  TRACE_INSTANT("cat", "name", TRACE_SCOPE_PROCESS);
  TRACE_INSTANT("cat", "name", TRACE_SCOPE_THREAD);
  TRACE_INSTANT("cat", "name", TRACE_SCOPE_THREAD, "k1", "v1");
  TRACE_INSTANT("cat", "name", TRACE_SCOPE_THREAD, "k1", "v1", "k2", "v2");
  TRACE_INSTANT("cat", "name", TRACE_SCOPE_THREAD, "k1", "v1", "k2", "v2", "k3",
                "v3");
  TRACE_INSTANT("cat", "name", TRACE_SCOPE_THREAD, "k1", "v1", "k2", "v2", "k3",
                "v3", "k4", "v4");
}

TEST_F(EventTest, Counter) {
  TRACE_COUNTER("cat", "name", 1, "k1", 1);
  TRACE_COUNTER("cat", "name", 1, "k1", 1, "k2", 2);
  TRACE_COUNTER("cat", "name", 1, "k1", 1, "k2", 2, "k3", 3);
  TRACE_COUNTER("cat", "name", 1, "k1", 1, "k2", 2, "k3", 3, "k4", 4);
}

TEST_F(EventTest, Duration) {
  TRACE_DURATION("cat", "name");
  TRACE_DURATION("cat", "name", "k1", "v1");
  TRACE_DURATION("cat", "name", "k1", "v1", "k2", "v2");
  TRACE_DURATION("cat", "name", "k1", "v1", "k2", "v2", "k3", "v3");
  TRACE_DURATION("cat", "name", "k1", "v1", "k2", "v2", "k3", "v3", "k4", "v4");
}

TEST_F(EventTest, DurationBegin) {
  TRACE_DURATION_BEGIN("cat", "name");
  TRACE_DURATION_BEGIN("cat", "name", "k1", "v1");
  TRACE_DURATION_BEGIN("cat", "name", "k1", "v1", "k2", "v2");
  TRACE_DURATION_BEGIN("cat", "name", "k1", "v1", "k2", "v2", "k3", "v3");
  TRACE_DURATION_BEGIN("cat", "name", "k1", "v1", "k2", "v2", "k3", "v3", "k4",
                       "v4");
}

TEST_F(EventTest, DurationEnd) {
  TRACE_DURATION_END("cat", "name");
  TRACE_DURATION_END("cat", "name", "k1", "v1");
  TRACE_DURATION_END("cat", "name", "k1", "v1", "k2", "v2");
  TRACE_DURATION_END("cat", "name", "k1", "v1", "k2", "v2", "k3", "v3");
  TRACE_DURATION_END("cat", "name", "k1", "v1", "k2", "v2", "k3", "v3", "k4",
                     "v4");
}

TEST_F(EventTest, AsyncBegin) {
  TRACE_ASYNC_BEGIN("cat", "name", 1);
  TRACE_ASYNC_BEGIN("cat", "name", 1, "k1", "v1");
  TRACE_ASYNC_BEGIN("cat", "name", 1, "k1", "v1", "k2", "v2");
  TRACE_ASYNC_BEGIN("cat", "name", 1, "k1", "v1", "k2", "v2", "k3", "v3");
  TRACE_ASYNC_BEGIN("cat", "name", 1, "k1", "v1", "k2", "v2", "k3", "v3", "k4",
                    "v4");
}

TEST_F(EventTest, AsyncInstant) {
  TRACE_ASYNC_INSTANT("cat", "name", 1);
  TRACE_ASYNC_INSTANT("cat", "name", 1, "k1", "v1");
  TRACE_ASYNC_INSTANT("cat", "name", 1, "k1", "v1", "k2", "v2");
  TRACE_ASYNC_INSTANT("cat", "name", 1, "k1", "v1", "k2", "v2", "k3", "v3");
  TRACE_ASYNC_INSTANT("cat", "name", 1, "k1", "v1", "k2", "v2", "k3", "v3",
                      "k4", "v4");
}

TEST_F(EventTest, AsyncEnd) {
  TRACE_ASYNC_END("cat", "name", 1);
  TRACE_ASYNC_END("cat", "name", 1, "k1", "v1");
  TRACE_ASYNC_END("cat", "name", 1, "k1", "v1", "k2", "v2");
  TRACE_ASYNC_END("cat", "name", 1, "k1", "v1", "k2", "v2", "k3", "v3");
  TRACE_ASYNC_END("cat", "name", 1, "k1", "v1", "k2", "v2", "k3", "v3", "k4",
                  "v4");
}

TEST_F(EventTest, FlowBegin) {
  TRACE_FLOW_BEGIN("cat", "name", 1);
  TRACE_FLOW_BEGIN("cat", "name", 1, "k1", "v1");
  TRACE_FLOW_BEGIN("cat", "name", 1, "k1", "v1", "k2", "v2");
  TRACE_FLOW_BEGIN("cat", "name", 1, "k1", "v1", "k2", "v2", "k3", "v3");
  TRACE_FLOW_BEGIN("cat", "name", 1, "k1", "v1", "k2", "v2", "k3", "v3", "k4",
                   "v4");
}

TEST_F(EventTest, FlowStep) {
  TRACE_FLOW_STEP("cat", "name", 1);
  TRACE_FLOW_STEP("cat", "name", 1, "k1", "v1");
  TRACE_FLOW_STEP("cat", "name", 1, "k1", "v1", "k2", "v2");
  TRACE_FLOW_STEP("cat", "name", 1, "k1", "v1", "k2", "v2", "k3", "v3");
  TRACE_FLOW_STEP("cat", "name", 1, "k1", "v1", "k2", "v2", "k3", "v3", "k4",
                  "v4");
}

TEST_F(EventTest, FlowEnd) {
  TRACE_FLOW_END("cat", "name", 1);
  TRACE_FLOW_END("cat", "name", 1, "k1", "v1");
  TRACE_FLOW_END("cat", "name", 1, "k1", "v1", "k2", "v2");
  TRACE_FLOW_END("cat", "name", 1, "k1", "v1", "k2", "v2", "k3", "v3");
  TRACE_FLOW_END("cat", "name", 1, "k1", "v1", "k2", "v2", "k3", "v3", "k4",
                 "v4");
}

TEST_F(EventTest, Handle) {
  TRACE_HANDLE(zx_process_self());
  TRACE_HANDLE(zx_process_self(), "k1", "v1");
  TRACE_HANDLE(zx_process_self(), "k1", "v1", "k2", "v2");
  TRACE_HANDLE(zx_process_self(), "k1", "v1", "k2", "v2", "k3", "v3");
  TRACE_HANDLE(zx_process_self(), "k1", "v1", "k2", "v2", "k3", "v3", "k4",
               "v4");
}

TEST_F(EventTest, NullArguments) {
  TRACE_DURATION_BEGIN("cat", "name", "key", nullptr);
}

TEST_F(EventTest, IntegralArguments) {
  TRACE_DURATION_BEGIN("cat", "name", "key", bool(true));
  TRACE_DURATION_BEGIN("cat", "name", "key", bool(false));

  TRACE_DURATION_BEGIN("cat", "name", "key", int8_t(INT8_MIN));
  TRACE_DURATION_BEGIN("cat", "name", "key", int8_t(INT8_MAX));
  TRACE_DURATION_BEGIN("cat", "name", "key", int16_t(INT16_MIN));
  TRACE_DURATION_BEGIN("cat", "name", "key", int16_t(INT16_MAX));
  TRACE_DURATION_BEGIN("cat", "name", "key", int32_t(INT32_MIN));
  TRACE_DURATION_BEGIN("cat", "name", "key", int32_t(INT32_MAX));
  TRACE_DURATION_BEGIN("cat", "name", "key", uint32_t(0));
  TRACE_DURATION_BEGIN("cat", "name", "key", uint32_t(UINT32_MAX));
  TRACE_DURATION_BEGIN("cat", "name", "key", int64_t(INT64_MIN));
  TRACE_DURATION_BEGIN("cat", "name", "key", int64_t(INT64_MAX));
  TRACE_DURATION_BEGIN("cat", "name", "key", uint64_t(0));
  TRACE_DURATION_BEGIN("cat", "name", "key", uint64_t(UINT64_MAX));
  TRACE_DURATION_BEGIN("cat", "name", "key", uint8_t(0));
  TRACE_DURATION_BEGIN("cat", "name", "key", uint8_t(UINT8_MAX));
  TRACE_DURATION_BEGIN("cat", "name", "key", uint16_t(0));
  TRACE_DURATION_BEGIN("cat", "name", "key", uint16_t(UINT16_MAX));
  TRACE_DURATION_BEGIN("cat", "name", "key", uint32_t(0));
  TRACE_DURATION_BEGIN("cat", "name", "key", uint32_t(UINT32_MAX));
  TRACE_DURATION_BEGIN("cat", "name", "key", uint64_t(0));
  TRACE_DURATION_BEGIN("cat", "name", "key", uint64_t(UINT64_MAX));
}

TEST_F(EventTest, EnumArguments) {
  enum class Int8Enum : int8_t { kZero, kOne };
  enum class UInt8Enum : uint8_t { kZero, kOne };
  enum class Int16Enum : int16_t { kZero, kOne };
  enum class UInt16Enum : uint16_t { kZero, kOne };
  enum class Int32Enum : int32_t { kZero, kOne };
  enum class UInt32Enum : uint32_t { kZero, kOne };
  enum class Int64Enum : int64_t { kZero, kOne };
  enum class UInt64Enum : uint64_t { kZero, kOne };

  TRACE_DURATION_BEGIN("cat", "name", "key", Int8Enum::kOne);
  TRACE_DURATION_BEGIN("cat", "name", "key", UInt8Enum::kOne);
  TRACE_DURATION_BEGIN("cat", "name", "key", Int16Enum::kOne);
  TRACE_DURATION_BEGIN("cat", "name", "key", UInt16Enum::kOne);
  TRACE_DURATION_BEGIN("cat", "name", "key", Int32Enum::kOne);
  TRACE_DURATION_BEGIN("cat", "name", "key", UInt32Enum::kOne);
  TRACE_DURATION_BEGIN("cat", "name", "key", Int64Enum::kOne);
  TRACE_DURATION_BEGIN("cat", "name", "key", UInt64Enum::kOne);
}

TEST_F(EventTest, FloatArguments) {
  TRACE_DURATION_BEGIN("cat", "name", "key", float(1.f));
  TRACE_DURATION_BEGIN("cat", "name", "key", double(1.));
}

TEST_F(EventTest, StringArguments) {
  const char* kNull = nullptr;
  const char* kConstChar = "const char*";
  const char kCharArray[] = "char[n]";
  std::string kString = "std::string";

  TRACE_DURATION_BEGIN("cat", "name", "key", kNull);
  TRACE_DURATION_BEGIN("cat", "name", "key", kConstChar);
  TRACE_DURATION_BEGIN("cat", "name", "key", kCharArray);
  TRACE_DURATION_BEGIN("cat", "name", "key", kString);
}

TEST_F(EventTest, PointerArguments) {
  void* kNull = nullptr;
  const void* kConstNull = nullptr;
  volatile void* kVolatileNull = nullptr;
  const volatile void* kConstVolatileNull = nullptr;
  void* kPtr = &kNull;
  const void* kConstPtr = &kNull;
  volatile void* kVolatilePtr = &kNull;
  const volatile void* kConstVolatilePtr = &kNull;

  TRACE_DURATION_BEGIN("cat", "name", "key", kNull);
  TRACE_DURATION_BEGIN("cat", "name", "key", kConstNull);
  TRACE_DURATION_BEGIN("cat", "name", "key", kVolatileNull);
  TRACE_DURATION_BEGIN("cat", "name", "key", kConstVolatileNull);
  TRACE_DURATION_BEGIN("cat", "name", "key", kPtr);
  TRACE_DURATION_BEGIN("cat", "name", "key", kConstPtr);
  TRACE_DURATION_BEGIN("cat", "name", "key", kVolatilePtr);
  TRACE_DURATION_BEGIN("cat", "name", "key", kConstVolatilePtr);
}

TEST_F(EventTest, KoidArguments) {
  TRACE_DURATION_BEGIN("cat", "name", "key", TRACE_KOID(42u));
}

}  // namespace
}  // namespace writer
}  // namespace tracing
