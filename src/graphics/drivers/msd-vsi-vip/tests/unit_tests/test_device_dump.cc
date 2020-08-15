// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <array>

#include <gtest/gtest.h>

#include "src/graphics/drivers/msd-vsi-vip/src/address_space_layout.h"
#include "src/graphics/drivers/msd-vsi-vip/src/command_buffer.h"
#include "test_command_buffer.h"

class TestDeviceDump : public TestCommandBuffer {
 protected:
  class FormattedString {
   public:
    FormattedString(const char* fmt, ...) {
      va_list args;
      va_start(args, fmt);
      int size = std::vsnprintf(nullptr, 0, fmt, args);
      buf_ = std::vector<char>(size + 1);
      std::vsnprintf(buf_.data(), buf_.size(), fmt, args);
      va_end(args);
    }

    const char* data() const { return buf_.data(); }

   private:
    std::vector<char> buf_;
  };

  // Creates the buffer and command buffer from |buffer_desc| and |sequence_number|.
  void CreateCommandBuffer(std::shared_ptr<MsdVsiContext> context, const BufferDesc& buffer_desc,
                           uint64_t sequence_number, std::shared_ptr<MsdVsiBuffer>* out_buffer,
                           std::unique_ptr<CommandBuffer>* out_batch);

  // Returns whether |match_strings| are present in the same order in |dump_string|.
  static bool FindStrings(const std::vector<std::string>& dump_string,
                          const std::vector<FormattedString>& match_strings) {
    DASSERT(match_strings.size() > 0);

    unsigned int num_matched = 0;
    for (auto& str : dump_string) {
      if (strstr(str.c_str(), match_strings[num_matched].data())) {
        num_matched++;
        if (num_matched == match_strings.size()) {
          return true;
        }
      }
    }
    DMESSAGE("Could not find %s, dump contains: \n", match_strings[num_matched].data());
    for (auto& str : dump_string) {
      DMESSAGE("%s", str.c_str());
    }
    return false;
  }
};

void TestDeviceDump::CreateCommandBuffer(std::shared_ptr<MsdVsiContext> context,
                                         const BufferDesc& buffer_desc, uint64_t sequence_number,
                                         std::shared_ptr<MsdVsiBuffer>* out_buffer,
                                         std::unique_ptr<CommandBuffer>* out_batch) {
  std::shared_ptr<MsdVsiBuffer> buffer;
  ASSERT_NO_FATAL_FAILURE(CreateAndMapBuffer(
      context, buffer_desc.buffer_size, buffer_desc.map_page_count, buffer_desc.gpu_addr, &buffer));

  std::unique_ptr<CommandBuffer> batch;
  ASSERT_NO_FATAL_FAILURE(CreateAndPrepareBatch(context, buffer, buffer_desc.data_size,
                                                buffer_desc.batch_offset, nullptr /* signal */,
                                                std::nullopt /* csb */, &batch));
  ASSERT_TRUE(batch->IsValidBatch());
  batch->SetSequenceNumber(sequence_number);

  *out_buffer = buffer;
  *out_batch = std::move(batch);
}

TEST_F(TestDeviceDump, DumpBasic) {
  MsdVsiDevice::DumpState dump_state;
  device_->Dump(&dump_state, false /* fault_present */);
  EXPECT_EQ(dump_state.last_completed_sequence_number, 0u);
  EXPECT_EQ(dump_state.last_submitted_sequence_number, 0u);
  EXPECT_TRUE(dump_state.idle);
  EXPECT_FALSE(dump_state.page_table_arrays_enabled);
  EXPECT_TRUE(dump_state.inflight_batches.empty());

  std::vector<std::string> dump_string;
  device_->FormatDump(&dump_state, &dump_string);

  // The exec address should only be printed after the page table arrays have been enabled.
  std::vector<FormattedString> match_strings = {
      FormattedString("idle: true"),
      FormattedString("current_execution_address: N/A", dump_state.exec_addr),
      FormattedString("No mmu exception detected")};
  ASSERT_TRUE(FindStrings(dump_string, match_strings));

  dump_state.idle = false;
  dump_state.page_table_arrays_enabled = true;
  dump_state.exec_addr = 0x10000;

  dump_state.fault_present = true;
  dump_state.fault_type = 2;
  dump_state.fault_gpu_address = 0x1234;

  device_->FormatDump(&dump_state, &dump_string);

  {
    std::vector<FormattedString> match_strings = {
        FormattedString("idle: false"),
        FormattedString("current_execution_address: 0x%x", dump_state.exec_addr),
        FormattedString("MMU EXCEPTION DETECTED\n"
                        "type 0x2 (page not present) gpu_address 0x1234")};

    ASSERT_TRUE(FindStrings(dump_string, match_strings));
  }
}

TEST_F(TestDeviceDump, DumpCommandBuffer) {
  BufferDesc desc = {.buffer_size = 0x2000,
                     .map_page_count = 2,
                     .data_size = 0x1000,
                     .batch_offset = 0x0,
                     .gpu_addr = 0x10000};
  constexpr uint64_t seq_num = 1;

  std::shared_ptr<MsdVsiBuffer> buf;
  std::unique_ptr<CommandBuffer> batch;
  ASSERT_NO_FATAL_FAILURE(CreateCommandBuffer(default_context(), desc, seq_num, &buf, &batch));

  uint32_t event;
  EXPECT_TRUE(device_->AllocInterruptEvent(true /* free_on_complete */, &event));
  EXPECT_TRUE(device_->WriteInterruptEvent(event, std::move(batch), default_address_space()));

  MsdVsiDevice::DumpState dump_state;
  device_->Dump(&dump_state, false /* fault_present */);

  // Set the exec address to lie within the batch buffer.
  dump_state.exec_addr = 0x10000;

  std::vector<std::string> dump_string;
  device_->FormatDump(&dump_state, &dump_string);

  std::vector<FormattedString> match_strings = {
      FormattedString("Exec Gpu Address 0x%lx", dump_state.exec_addr),
  };
  ASSERT_TRUE(FindStrings(dump_string, match_strings));

  // Should not see any fault information.
  match_strings = {
      FormattedString("FAULTING BATCH"),
  };
  ASSERT_FALSE(FindStrings(dump_string, match_strings));
}

TEST_F(TestDeviceDump, DumpCommandBufferWithFault) {
  // Add some in-flight batches at different gpu addresses.
  BufferDesc desc1 = {.buffer_size = 0x1000,
                      .map_page_count = 1,
                      .data_size = 0x10,
                      .batch_offset = 0x0,
                      .gpu_addr = 0x10000};
  BufferDesc desc2 = {.buffer_size = 0x2000,
                      .map_page_count = 2,
                      .data_size = 0x10,
                      .batch_offset = 0x1000,
                      .gpu_addr = 0x20000};

  constexpr uint64_t seq_num1 = 10;
  constexpr uint64_t seq_num2 = 11;

  std::shared_ptr<MsdVsiBuffer> buf1;
  std::unique_ptr<CommandBuffer> batch1;
  ASSERT_NO_FATAL_FAILURE(CreateCommandBuffer(default_context(), desc1, seq_num1, &buf1, &batch1));

  std::shared_ptr<MsdVsiBuffer> buf2;
  std::unique_ptr<CommandBuffer> batch2;
  ASSERT_NO_FATAL_FAILURE(CreateCommandBuffer(default_context(), desc2, seq_num2, &buf2, &batch2));

  uint32_t event1;
  uint32_t event2;
  EXPECT_TRUE(device_->AllocInterruptEvent(true /* free_on_complete */, &event1));
  EXPECT_TRUE(device_->AllocInterruptEvent(true /* free_on_complete */, &event2));
  // Write the event numbers in opposite order to the batch sequence numbers to verify the
  // batches are still outputted in the correct order.
  EXPECT_TRUE(device_->WriteInterruptEvent(event2, std::move(batch1), default_address_space()));
  EXPECT_TRUE(device_->WriteInterruptEvent(event1, std::move(batch2), default_address_space()));

  MsdVsiDevice::DumpState dump_state;
  device_->Dump(&dump_state, true /* fault_present */);
  // Set the exec address to lie within the second batch buffer.
  dump_state.exec_addr = 0x20004;

  std::vector<std::string> dump_string;
  device_->FormatDump(&dump_state, &dump_string);

  std::vector<FormattedString> match_strings = {
      FormattedString("Batch %lu (Command)", seq_num1),
      FormattedString("Exec Gpu Address 0x%lx", desc1.gpu_addr + desc1.batch_offset),
      FormattedString("buffer 0x%lx", buf1->platform_buffer()->id()),
      FormattedString("Batch %lu (Command)", seq_num2),
      FormattedString("FAULTING BATCH"),
      FormattedString("Exec Gpu Address 0x%lx", desc2.gpu_addr + desc2.batch_offset),
      FormattedString("buffer 0x%lx", buf2->platform_buffer()->id()),
  };

  ASSERT_TRUE(FindStrings(dump_string, match_strings));
}

TEST_F(TestDeviceDump, DumpEventBatch) {
  const uint64_t seq_num = 1;

  std::vector<std::shared_ptr<magma::PlatformSemaphore>> wait_semaphores;
  std::vector<std::shared_ptr<magma::PlatformSemaphore>> signal_semaphores;
  auto batch = std::make_unique<EventBatch>(default_context(), std::move(wait_semaphores),
                                            std::move(signal_semaphores));
  batch->SetSequenceNumber(seq_num);

  uint32_t event;
  EXPECT_TRUE(device_->AllocInterruptEvent(true /* free_on_complete */, &event));
  EXPECT_TRUE(device_->WriteInterruptEvent(event, std::move(batch), default_address_space()));

  MsdVsiDevice::DumpState dump_state;
  device_->Dump(&dump_state, false /* fault_present */);

  std::vector<std::string> dump_string;
  device_->FormatDump(&dump_state, &dump_string);

  std::vector<FormattedString> match_strings = {
      FormattedString("Batch %lu (Event)", seq_num),
  };
  ASSERT_TRUE(FindStrings(dump_string, match_strings));
}

TEST_F(TestDeviceDump, DumpCommandBufferMultipleResources) {
  // Create one command buffer with three resources.
  constexpr uint32_t kResourcesCount = 3;

  BufferDesc desc1 = {.buffer_size = 0x2000,
                      .map_page_count = 1,
                      .data_size = 0x1000,
                      .batch_offset = 0x0,
                      .gpu_addr = 0x20000};
  BufferDesc desc2 = {.buffer_size = 0x2000,
                      .map_page_count = 1,
                      .data_size = 0x1000,
                      .batch_offset = 0x0,
                      .gpu_addr = 0x40000};
  BufferDesc desc3 = {.buffer_size = 0x2000,
                      .map_page_count = 1,
                      .data_size = 0x1000,
                      .batch_offset = 0x0,
                      .gpu_addr = 0x30000};

  std::array<BufferDesc, kResourcesCount> descs = {desc1, desc2, desc3};

  std::array<std::shared_ptr<MsdVsiBuffer>, kResourcesCount> bufs;

  for (unsigned int i = 0; i < kResourcesCount; i++) {
    const auto& desc = descs[i];
    ASSERT_NO_FATAL_FAILURE(CreateAndMapBuffer(default_context(), desc.buffer_size,
                                               desc.map_page_count, desc.gpu_addr, &bufs[i]));
  }

  auto command_buffer = std::make_unique<magma_system_command_buffer>(magma_system_command_buffer{
      .resource_count = kResourcesCount,
      .batch_buffer_resource_index = 0,
      .batch_start_offset = 0,
      .wait_semaphore_count = 0,
      .signal_semaphore_count = 0,
  });
  auto batch = std::make_unique<CommandBuffer>(default_context(), 0, std::move(command_buffer));
  ASSERT_NE(batch, nullptr);

  std::vector<CommandBuffer::ExecResource> resources;
  for (unsigned int i = 0; i < kResourcesCount; i++) {
    resources.emplace_back(
        CommandBuffer::ExecResource{.buffer = bufs[i], .offset = 0, .length = descs[i].data_size});
  }

  std::vector<std::shared_ptr<magma::PlatformSemaphore>> wait_semaphores;
  std::vector<std::shared_ptr<magma::PlatformSemaphore>> signal_semaphores;
  ASSERT_TRUE(batch->InitializeResources(std::move(resources), std::move(wait_semaphores),
                                         std::move(signal_semaphores)));
  ASSERT_TRUE(batch->PrepareForExecution());

  std::vector<GpuMappingView*> mappings;
  batch->GetMappings(&mappings);

  uint32_t event;
  EXPECT_TRUE(device_->AllocInterruptEvent(true /* free_on_complete */, &event));
  EXPECT_TRUE(device_->WriteInterruptEvent(event, std::move(batch), default_address_space()));

  MsdVsiDevice::DumpState dump_state;
  device_->Dump(&dump_state, true /* fault_present */);

  // Set the fault address to lie within the third resource.
  dump_state.fault_gpu_address = descs[2].gpu_addr;

  std::vector<std::string> dump_string;
  device_->FormatDump(&dump_state, &dump_string);

  std::vector<FormattedString> match_strings = {
      FormattedString("Fault address appears to be within mapping %p", mappings[2]),
  };
  ASSERT_TRUE(FindStrings(dump_string, match_strings));

  // Set the fault address to lie past the end of the second resource.
  dump_state.fault_gpu_address = 0x50000;

  dump_string.clear();
  device_->FormatDump(&dump_state, &dump_string);

  uint32_t mapping_end = descs[1].gpu_addr + descs[1].map_page_count * magma::page_size();
  match_strings = {
      FormattedString("Fault address is 0x%lx past the end of mapping %p",
                      dump_state.fault_gpu_address - mapping_end, mappings[1]),
  };
  ASSERT_TRUE(FindStrings(dump_string, match_strings));
}

// Tests the decoding for each instruction type.
TEST_F(TestDeviceDump, DumpDecodedBuffer) {
  constexpr uint32_t link_addr = 0x10000;

  constexpr uint32_t buf_size_dwords = 7 * kInstructionDwords;
  uint32_t buf[buf_size_dwords];
  BufferWriter buf_writer(buf, buf_size_dwords * sizeof(uint32_t), 0);

  MiLink::write(&buf_writer, 8 /* prefetch */, link_addr);
  MiWait::write(&buf_writer);
  MiLoadState::write(&buf_writer, 1, 2);
  MiEvent::write(&buf_writer, 1);
  MiSemaphore::write(&buf_writer, 1, 2, 3);
  MiStall::write(&buf_writer, 1, 2, 3);
  MiEnd::write(&buf_writer);

  std::vector<std::string> dump_string;
  device_->DumpDecodedBuffer(&dump_string, buf, buf_size_dwords, 0 /* start_dword */,
                             buf_size_dwords /* dword_count */, 4 /* active_head_dword */);

  std::vector<FormattedString> match_strings = {
      FormattedString("LINK"),  FormattedString("%08lx", link_addr),
      FormattedString("WAIT"),  FormattedString("LOAD_STATE"),
      FormattedString("===>"),  // matches active_head_dword
      FormattedString("EVENT"), FormattedString("SEMAPHORE"),
      FormattedString("STALL"), FormattedString("END")};
  ASSERT_TRUE(FindStrings(dump_string, match_strings));
}

TEST_F(TestDeviceDump, DumpRingbufferWithWraparound) {
  // Ringbuffer layout:
  // SEMAPHORE STALL END ....... EVENT LINK
  //             |                 |
  //            active_head      last_completed_event
  const uint32_t active_head = AddressSpaceLayout::system_gpu_addr_base() + 0x8;
  // Start the ringbuffer at 2 instructions from the end.
  device_->ringbuffer_->Reset(4080);
  MiEvent::write(device_->ringbuffer_.get(), 1);
  MiLink::write(device_->ringbuffer_.get(), 8, 0x10000);
  MiSemaphore::write(device_->ringbuffer_.get(), 1, 2, 3);
  MiStall::write(device_->ringbuffer_.get(), 1, 2, 3);
  MiEnd::write(device_->ringbuffer_.get());
  // Update the head past the event.
  device_->ringbuffer_->update_head(4088);

  MsdVsiDevice::DumpState dump_state;
  dump_state.exec_addr = active_head;
  dump_state.page_table_arrays_enabled = true;

  std::vector<std::string> dump_string;
  device_->FormatDump(&dump_state, &dump_string);

  std::vector<FormattedString> match_strings = {
      FormattedString("LINK"), FormattedString("SEMAPHORE"), FormattedString("STALL"),
      FormattedString("===>"),  // matches active_head_dword
      FormattedString("END")};
  ASSERT_TRUE(FindStrings(dump_string, match_strings));
}
