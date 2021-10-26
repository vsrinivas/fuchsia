// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <gtest/gtest.h>

#include "magma_util/macros.h"
#include "magma_util/register_io.h"
#include "src/graphics/lib/magma/tests/mock/mock_mmio.h"

namespace {
class RegisterTracer : public magma::RegisterIo::Hook {
 public:
  struct Operation {
    enum Type { WRITE32, READ32, READ64 };
    Type type;
    uint32_t offset;
    uint64_t val;
  };

  std::vector<Operation>& trace() { return trace_; }

  void Write32(uint32_t val, uint32_t offset) override {
    trace_.emplace_back(Operation{Operation::WRITE32, offset, val});
  }
  void Read32(uint32_t val, uint32_t offset) override {
    trace_.emplace_back(Operation{Operation::READ32, offset, val});
  }
  void Read64(uint64_t val, uint32_t offset) override {
    trace_.emplace_back(Operation{Operation::READ64, offset, val});
  }

 private:
  std::vector<Operation> trace_;
};

TEST(RegisterIo, TemplatedHooks) {
  magma::RegisterIo register_io(MockMmio::Create(4096));

  auto tracer = std::make_unique<RegisterTracer>();
  RegisterTracer* tracer_ptr = tracer.get();
  register_io.InstallHook(std::move(tracer));

  EXPECT_EQ(0u, register_io.Read<uint32_t>(0u));
  EXPECT_EQ(0u, register_io.Read<uint64_t>(8u));

  constexpr uint32_t kWriteOffset = 12;

  register_io.Write(1, kWriteOffset);

  auto& trace = tracer_ptr->trace();
  ASSERT_EQ(3u, trace.size());
  EXPECT_EQ(RegisterTracer::Operation::READ32, trace[0].type);
  EXPECT_EQ(0u, trace[0].val);
  EXPECT_EQ(0u, trace[0].offset);
  EXPECT_EQ(RegisterTracer::Operation::READ64, trace[1].type);
  EXPECT_EQ(0u, trace[1].val);
  EXPECT_EQ(8u, trace[1].offset);
  EXPECT_EQ(RegisterTracer::Operation::WRITE32, trace[2].type);
  EXPECT_EQ(1u, trace[2].val);
  EXPECT_EQ(kWriteOffset, trace[2].offset);
}

}  // namespace
