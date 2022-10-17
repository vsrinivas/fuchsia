// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "intel-dsp-modules.h"

#include <lib/ddk/debug.h>
#include <lib/zx/time.h>
#include <threads.h>
#include <zircon/assert.h>
#include <zircon/errors.h>

#include <cstdint>
#include <unordered_set>
#include <vector>

#include <zxtest/zxtest.h>

#include "debug-logging.h"
#include "intel-dsp-ipc.h"

namespace audio::intel_hda {
namespace {

// A DSP channel that always succeeds its IPCs and records the inputs to Send() operations.
class FakeDspChannel : public DspChannel {
 public:
  // An IPC that was sent.
  struct Ipc {
    uint32_t primary;
    uint32_t secondary;
    std::vector<uint8_t> data;

    bool operator==(const Ipc& other) const {
      return std::tie(primary, secondary, data) ==
             std::tie(other.primary, other.secondary, other.data);
    }
  };

  ~FakeDspChannel() {}
  void Shutdown() override {}
  void ProcessIrq() override {}
  bool IsOperationPending() const override { return false; }

  zx::result<> Send(uint32_t primary, uint32_t extension) override {
    return SendWithData(primary, extension, cpp20::span<const uint8_t>(), cpp20::span<uint8_t>(),
                        nullptr);
  }

  zx::result<> SendWithData(uint32_t primary, uint32_t extension,
                            cpp20::span<const uint8_t> payload, cpp20::span<uint8_t> recv_buffer,
                            size_t* bytes_received) override {
    ipcs_.push_back(Ipc{primary, extension, std::vector<uint8_t>(payload.begin(), payload.end())});
    return zx::ok();
  }

  const std::vector<Ipc>& ipcs() const { return ipcs_; }

 private:
  std::vector<Ipc> ipcs_;
};

TEST(DspModuleController, AllocatePipelineIds) {
  FakeDspChannel fake_channel;
  DspModuleController controller(&fake_channel);

  // Allocate 3 IDs. Expect them to be allocated from 0 upwards.
  EXPECT_EQ(0, controller.CreatePipeline(0, 0, 0).value().id);
  EXPECT_EQ(1, controller.CreatePipeline(0, 0, 0).value().id);
  EXPECT_EQ(2, controller.CreatePipeline(0, 0, 0).value().id);
}

TEST(DspModuleController, TooManyPipelines) {
  FakeDspChannel fake_channel;
  DspModuleController controller(&fake_channel);

  // Expect allocation to fail gracefully at some time, without duplicates.
  std::unordered_set<uint8_t> seen_ids;
  bool saw_error = false;
  for (int i = 0; i < 1000; i++) {
    zx::result<DspPipelineId> pipeline = controller.CreatePipeline(0, 0, 0);
    if (!pipeline.is_ok()) {
      saw_error = true;
      break;
    }
    auto id = pipeline.value().id;
    auto [_, inserted] = seen_ids.insert(id);
    // Ensure we hadn't seen this ID yet.
    EXPECT_TRUE(inserted);
  }

  EXPECT_TRUE(saw_error);
}

TEST(DspModuleController, AllocateModuleIds) {
  FakeDspChannel fake_channel;
  DspModuleController controller(&fake_channel);
  DspPipelineId pipeline = controller.CreatePipeline(0, 0, 0).value();

  // Allocate some module IDs. Expect them to be allocated from 0 upwards.
  for (int i = 0; i < 10; i++) {
    DspModuleId m =
        controller.CreateModule(/*type=*/42, pipeline, ProcDomain::LOW_LATENCY, {}).value();
    EXPECT_EQ(m.type, 42);
    EXPECT_EQ(m.id, i);
  }
}

TEST(DspModuleController, TooManyModules) {
  FakeDspChannel fake_channel;
  DspModuleController controller(&fake_channel);
  DspPipelineId pipeline = controller.CreatePipeline(0, 0, 0).value();

  // Expect allocation to fail gracefully at some time, without duplicates.
  std::unordered_set<uint8_t> seen_ids;
  bool saw_error = false;
  for (int i = 0; i < 1000; i++) {
    zx::result<DspModuleId> module = controller.CreateModule(
        /*type=*/42, pipeline, ProcDomain::LOW_LATENCY, {});
    if (!module.is_ok()) {
      saw_error = true;
      break;
    }
    auto id = module.value().id;
    auto [_, inserted] = seen_ids.insert(id);
    // Ensure we hadn't seen this ID yet.
    EXPECT_TRUE(inserted);
  }

  EXPECT_TRUE(saw_error);
}

TEST(DspModuleController, CreatePipelineIpc) {
  FakeDspChannel fake_channel;
  DspModuleController controller(&fake_channel);

  // Send the IPC.
  EXPECT_TRUE(
      controller.CreatePipeline(/*priority=*/1, /*memory_pages=*/2, /*low_power=*/true).is_ok());

  // Ensure the correct IPC was sent.
  ASSERT_EQ(fake_channel.ipcs().size(), 1);
  ASSERT_EQ(fake_channel.ipcs()[0],
            (FakeDspChannel::Ipc{
                IPC_CREATE_PIPELINE_PRI(/*instance_id=*/0, /*ppl_priority=*/1, /*ppl_memsize=*/2),
                IPC_CREATE_PIPELINE_EXT(/*lp=*/true),
                {}}));
}

TEST(DspModuleController, CreateModuleIpc) {
  FakeDspChannel fake_channel;
  DspModuleController controller(&fake_channel);

  // Send the IPC.
  uint8_t data[] = {1, 2, 3, 4};
  EXPECT_TRUE(controller
                  .CreateModule(/*type=*/42, /*parent_pipeline=*/{17},
                                /*scheduling_domain=*/ProcDomain::LOW_LATENCY, data)
                  .is_ok());

  // Ensure the correct IPC was sent.
  ASSERT_EQ(fake_channel.ipcs().size(), 1);
  ASSERT_EQ(
      fake_channel.ipcs()[0],
      (FakeDspChannel::Ipc{
          IPC_PRI(MsgTarget::MODULE_MSG, MsgDir::MSG_REQUEST, ModuleMsgType::INIT_INSTANCE,
                  /*instance_id=*/0, /*module_id=*/42),
          IPC_INIT_INSTANCE_EXT(ProcDomain::LOW_LATENCY, /*core_id=*/0, /*ppl_instance_id=*/17,
                                /*param_block_words=*/1),
          {1, 2, 3, 4}}));
}

TEST(DspModuleController, CreateModuleIpcBadDataSize) {
  FakeDspChannel fake_channel;
  DspModuleController controller(&fake_channel);

  // Send the IPC.
  uint8_t data[] = {1, 2, 3};  // Non word-sized data.
  EXPECT_FALSE(controller
                   .CreateModule(/*type=*/42, /*parent_pipeline=*/{17},
                                 /*scheduling_domain=*/ProcDomain::LOW_LATENCY, data)
                   .is_ok());
}

TEST(DspModuleController, CreateModuleIpcBigData) {
  FakeDspChannel fake_channel;
  DspModuleController controller(&fake_channel);

  // Create a large amount of data.
  std::vector<uint8_t> data;
  data.resize(1'000'000);

  // Try sending; we should get an error.
  EXPECT_EQ(controller
                .CreateModule(/*type=*/42, /*parent_pipeline=*/{17},
                              /*scheduling_domain=*/ProcDomain::LOW_LATENCY, data)
                .status_value(),
            ZX_ERR_INVALID_ARGS);
}

TEST(DspModuleController, BindModules) {
  FakeDspChannel fake_channel;
  DspModuleController controller(&fake_channel);

  // Send the IPC.
  DspModuleId source_module = {1, 2};
  DspModuleId dest_module = {3, 4};
  EXPECT_TRUE(
      controller.BindModules(source_module, /*src_output_pin=*/5, dest_module, /*dest_input_pin=*/6)
          .is_ok());

  // Ensure the correct IPC was sent.
  ASSERT_EQ(fake_channel.ipcs().size(), 1);
  ASSERT_EQ(fake_channel.ipcs()[0],
            (FakeDspChannel::Ipc{IPC_PRI(MsgTarget::MODULE_MSG, MsgDir::MSG_REQUEST,
                                         ModuleMsgType::BIND, /*instance_id=*/2,
                                         /*module_id=*/1),
                                 IPC_BIND_UNBIND_EXT(/*dst_module_id=*/3, /*dst_instance_id=*/4,
                                                     /*dst_queue=*/6, /*src_queue=*/5),
                                 {}}));
}

TEST(DspModuleController, SetPipelineState) {
  FakeDspChannel fake_channel;
  DspModuleController controller(&fake_channel);

  // Send the IPC.
  EXPECT_TRUE(
      controller.SetPipelineState(/*pipeline=*/{1}, PipelineState::RESET, /*sync_stop_start=*/true)
          .is_ok());

  // Ensure the correct IPC was sent.
  ASSERT_EQ(fake_channel.ipcs().size(), 1);
  ASSERT_EQ(fake_channel.ipcs()[0],
            (FakeDspChannel::Ipc{
                IPC_SET_PIPELINE_STATE_PRI(1, PipelineState::RESET),
                IPC_SET_PIPELINE_STATE_EXT(/*multi_ppl=*/false, /*sync_stop_start=*/true),
                {}}));
}

TEST(ParseModules, TruncatedData) {
  // Try parsing a range of bytes, where the data has been truncated.
  const int kMaxDataSize = sizeof(ModulesInfo) + sizeof(ModuleEntry) - 1;
  uint8_t buff[kMaxDataSize] = {};
  for (int i = 0; i < kMaxDataSize; i++) {
    ModulesInfo info{};
    info.module_count = 1;
    memcpy(buff, &info, sizeof(ModulesInfo));
    EXPECT_TRUE(!ParseModules(cpp20::span<uint8_t>(buff)).is_ok());
  }
}

TEST(ParseModules, RealData) {
  struct Data {
    ModulesInfo header;
    ModuleEntry entry1;
    ModuleEntry entry2;
  } __PACKED data;

  // Generate a set of 2 modules.
  data.header.module_count = 2;
  memcpy(data.entry1.name, "ABC\0", 4);
  data.entry1.module_id = 42;
  static_assert(sizeof(data.entry2.name) == 8);
  memcpy(data.entry2.name, "01234567", 8);  // Pack the entire 8 bytes for the name.
  data.entry2.module_id = 17;

  // Parse the modules.
  auto result =
      ParseModules(cpp20::span(reinterpret_cast<const uint8_t*>(&data), sizeof(data))).value();

  // Ensure both module entries appear in the output.
  EXPECT_EQ(result.size(), 2);

  auto a = result.find("ABC");
  ASSERT_TRUE(a != result.end());
  EXPECT_EQ(a->second->module_id, 42);

  auto b = result.find("01234567");
  ASSERT_TRUE(b != result.end());
  EXPECT_EQ(b->second->module_id, 17);
}

}  // namespace
}  // namespace audio::intel_hda
