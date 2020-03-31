// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "src/graphics/drivers/msd-vsl-gc/src/command_buffer.h"
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
  void CreateCommandBuffer(std::shared_ptr<MsdVslContext> context, const BufferDesc& buffer_desc,
                           uint64_t sequence_number, std::shared_ptr<MsdVslBuffer>* out_buffer,
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
    DMESSAGE("Could not find %s\n", match_strings[num_matched].data());
    return false;
  }
};

void TestDeviceDump::CreateCommandBuffer(std::shared_ptr<MsdVslContext> context,
                                         const BufferDesc& buffer_desc, uint64_t sequence_number,
                                         std::shared_ptr<MsdVslBuffer>* out_buffer,
                                         std::unique_ptr<CommandBuffer>* out_batch) {
  std::shared_ptr<MsdVslBuffer> buffer;
  ASSERT_NO_FATAL_FAILURE(CreateAndMapBuffer(
      context, buffer_desc.buffer_size, buffer_desc.map_page_count, buffer_desc.gpu_addr, &buffer));

  std::unique_ptr<CommandBuffer> batch;
  ASSERT_NO_FATAL_FAILURE(CreateAndPrepareBatch(context, buffer, buffer_desc.data_size,
                                                buffer_desc.batch_offset, nullptr /* signal */,
                                                &batch));
  ASSERT_TRUE(batch->IsValidBatchBuffer());
  batch->SetSequenceNumber(sequence_number);

  *out_buffer = buffer;
  *out_batch = std::move(batch);
}

TEST_F(TestDeviceDump, DumpBasic) {
  MsdVslDevice::DumpState dump_state;
  device_->Dump(&dump_state);
  EXPECT_EQ(dump_state.max_completed_sequence_number, 0u);
  EXPECT_EQ(dump_state.next_sequence_number, 1u);
  EXPECT_TRUE(dump_state.idle);
  EXPECT_FALSE(dump_state.page_table_arrays_enabled);
  EXPECT_TRUE(dump_state.inflight_batches.empty());

  std::vector<std::string> dump_string;
  device_->FormatDump(&dump_state, &dump_string);

  // The exec address should only be printed after the page table arrays have been enabled.
  std::vector<FormattedString> match_strings = {
      FormattedString("idle: true"),
      FormattedString("current_execution_address: N/A", dump_state.exec_addr)};
  ASSERT_TRUE(FindStrings(dump_string, match_strings));

  dump_state.idle = false;
  dump_state.page_table_arrays_enabled = true;
  dump_state.exec_addr = 0x10000;

  device_->FormatDump(&dump_state, &dump_string);

  match_strings = {FormattedString("idle: false"),
                   FormattedString("current_execution_address: 0x%x", dump_state.exec_addr)};
  ASSERT_TRUE(FindStrings(dump_string, match_strings));
}

TEST_F(TestDeviceDump, DumpCommandBuffer) {
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

  std::shared_ptr<MsdVslBuffer> buf1;
  std::unique_ptr<CommandBuffer> batch1;
  ASSERT_NO_FATAL_FAILURE(CreateCommandBuffer(default_context(), desc1, seq_num1, &buf1, &batch1));

  std::shared_ptr<MsdVslBuffer> buf2;
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

  MsdVslDevice::DumpState dump_state;
  device_->Dump(&dump_state);

  std::vector<std::string> dump_string;
  device_->FormatDump(&dump_state, &dump_string);

  std::vector<FormattedString> match_strings = {
      FormattedString("Batch %lu (Command)", seq_num1),
      FormattedString("Exec Gpu Address 0x%lx", desc1.gpu_addr + desc1.batch_offset),
      FormattedString("buffer 0x%lx", buf1->platform_buffer()->id()),
      FormattedString("Batch %lu (Command)", seq_num2),
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

  MsdVslDevice::DumpState dump_state;
  device_->Dump(&dump_state);

  std::vector<std::string> dump_string;
  device_->FormatDump(&dump_state, &dump_string);

  std::vector<FormattedString> match_strings = {
      FormattedString("Batch %lu (Event)", seq_num),
  };
  ASSERT_TRUE(FindStrings(dump_string, match_strings));
}
