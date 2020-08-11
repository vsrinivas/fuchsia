// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "helper/platform_device_helper.h"
#include "src/graphics/drivers/msd-vsi-vip/src/command_buffer.h"
#include "src/graphics/drivers/msd-vsi-vip/src/msd_vsi_device.h"

class TestCommandBuffer : public ::testing::Test {
 public:
  static constexpr uint32_t kAddressSpaceIndex = 1;

  struct BufferDesc {
    uint32_t buffer_size;
    uint32_t map_page_count;
    uint32_t data_size;
    uint32_t batch_offset;
    uint32_t gpu_addr;
  };

  void SetUp() override {
    device_ = MsdVsiDevice::Create(GetTestDeviceHandle(), false /* start_device_thread */);
    ASSERT_NE(device_, nullptr);
    ASSERT_TRUE(device_->IsIdle());

    address_space_owner_ = std::make_unique<AddressSpaceOwner>(device_->GetBusMapper());
    ASSERT_NO_FATAL_FAILURE(CreateClient(kAddressSpaceIndex, &client_));
  }

  void Release() {
    client_.reset();
    address_space_owner_.reset();
    device_.reset();
  }

 protected:
  // This holds the buffer that can be passed as the context state buffer for a batch.
  class FakeContextStateBuffer {
   public:
    // Returns a fake context state buffer in |out_buffer|, which has an EVENT command written
    // to the underlying platform buffer. After submitting the batch, the caller can verify the
    // buffer was executed by calling |WaitForCompletion|, which will wait for the EVENT
    // to be executed.
    static void CreateWithEvent(std::unique_ptr<MsdVsiDevice>& device,
                                std::shared_ptr<MsdVsiContext> context, uint32_t gpu_addr,
                                std::unique_ptr<FakeContextStateBuffer>* out_buffer) {
      constexpr uint32_t kBufferSize = 4096;
      constexpr uint32_t kMapPageCount = 1;
      constexpr uint32_t kDataSize = 8;  // EVENT command.

      std::shared_ptr<MsdVsiBuffer> buf;
      std::unique_ptr<magma::PlatformSemaphore> semaphore;
      ASSERT_NO_FATAL_FAILURE(
          CreateAndMapBuffer(context, kBufferSize, kMapPageCount, gpu_addr, &buf));
      ASSERT_NO_FATAL_FAILURE(WriteEventCommand(device, context, buf, 0 /* offset */, &semaphore));
      *out_buffer =
          std::make_unique<FakeContextStateBuffer>(std::move(buf), kDataSize, std::move(semaphore));
    }

    FakeContextStateBuffer(std::shared_ptr<MsdVsiBuffer> buf, uint32_t data_size,
                           std::unique_ptr<magma::PlatformSemaphore> semaphore)
        : buf_(buf), data_size_(data_size), semaphore_(std::move(semaphore)) {}

    CommandBuffer::ExecResource ExecResource() {
      return CommandBuffer::ExecResource{.buffer = buf_, .offset = 0, .length = data_size_};
    }

    void WaitForCompletion() {
      constexpr uint64_t kTimeoutMs = 1000;
      ASSERT_EQ(MAGMA_STATUS_OK, semaphore_->Wait(kTimeoutMs).get());
    }

   private:
    std::shared_ptr<MsdVsiBuffer> buf_;
    uint32_t data_size_;
    std::unique_ptr<magma::PlatformSemaphore> semaphore_;
  };

  class AddressSpaceOwner : public AddressSpace::Owner {
   public:
    AddressSpaceOwner(magma::PlatformBusMapper* bus_mapper) : bus_mapper_(bus_mapper) {}
    virtual ~AddressSpaceOwner() = default;

    void AddressSpaceReleased(AddressSpace* address_space) override {}

    magma::PlatformBusMapper* GetBusMapper() override { return bus_mapper_; }

   private:
    magma::PlatformBusMapper* bus_mapper_;
  };

  struct Client {
    std::shared_ptr<MsdVsiConnection> connection;
    std::shared_ptr<MsdVsiContext> context;
    std::shared_ptr<AddressSpace> address_space;

    Client(std::shared_ptr<MsdVsiConnection> connection, std::shared_ptr<MsdVsiContext> context,
           std::shared_ptr<AddressSpace> address_space)
        : connection(connection), context(context), address_space(address_space) {}
  };

  void CreateClient(uint32_t address_space_index, std::unique_ptr<Client>* out_client) {
    std::shared_ptr<AddressSpace> address_space =
        AddressSpace::Create(address_space_owner_.get(), address_space_index);
    ASSERT_NE(address_space, nullptr);

    device_->page_table_arrays()->AssignAddressSpace(address_space_index, address_space.get());

    auto connection =
        std::make_shared<MsdVsiConnection>(device_.get(), address_space, 1 /* client_id */);
    ASSERT_NE(connection, nullptr);
    auto context = MsdVsiContext::Create(connection, address_space, device_->GetRingbuffer());
    ASSERT_NE(context, nullptr);
    *out_client = std::make_unique<Client>(connection, context, address_space);
  }

  // Creates a buffer of |buffer_size| and stores it in |out_buffer|.
  static void CreateMsdBuffer(uint32_t buffer_size, std::shared_ptr<MsdVsiBuffer>* out_buffer);

  // Creates a buffer of |buffer_size| bytes, and maps the buffer to |gpu_addr|.
  // |map_page_count| may be less bytes than buffer size.
  static void CreateAndMapBuffer(std::shared_ptr<MsdVsiContext> context, uint32_t buffer_size,
                                 uint32_t map_page_count, uint32_t gpu_addr,
                                 std::shared_ptr<MsdVsiBuffer>* out_buffer);

  // Creates a new command buffer.
  // |data_size| is the actual length of the user provided data and may be smaller than
  // the size of |buffer|.
  // |signal| is an optional semaphore. If present, it will be signalled after the batch
  // is submitted via |SubmitBatch| and execution completes.
  void CreateAndPrepareBatch(std::shared_ptr<MsdVsiContext> context,
                             std::shared_ptr<MsdVsiBuffer> buffer, uint32_t data_size,
                             uint32_t batch_offset,
                             std::shared_ptr<magma::PlatformSemaphore> signal,
                             std::optional<CommandBuffer::ExecResource> context_state_buffer,
                             std::unique_ptr<CommandBuffer>* out_batch);

  // Creates a buffer from |buffer_desc|, writes a test instruction to it and
  // submits it as a command buffer.
  // |signal| is an optional semaphore that will be passed as a signal semaphore for the batch.
  // If |fault_addr| is populated, the submitted buffer will contain faulting instructions.
  // If |out_buffer| is non-null, it will be populated with the created buffer.
  void CreateAndSubmitBuffer(
      std::shared_ptr<MsdVsiContext> context, const BufferDesc& buffer_desc,
      std::shared_ptr<magma::PlatformSemaphore> signal, std::optional<uint32_t> fault_addr,
      std::optional<CommandBuffer::ExecResource> context_state_buffer = std::nullopt,
      std::shared_ptr<MsdVsiBuffer>* out_buffer = nullptr);

  // Creates a buffer from |buffer_desc|, writes a test instruction to it and
  // submits it as a command buffer. This will wait for execution to complete.
  // If |out_buffer| is non-null, it will be populated with the created buffer.
  void CreateAndSubmitBuffer(
      std::shared_ptr<MsdVsiContext> context, const BufferDesc& buffer_desc,
      std::optional<CommandBuffer::ExecResource> context_state_buffer = std::nullopt,
      std::shared_ptr<MsdVsiBuffer>* out_buffer = nullptr);

  // Writes a single WAIT command in |buf| at |offset|.
  void WriteWaitCommand(std::shared_ptr<MsdVsiBuffer> buf, uint32_t offset);

  // Writes a single LINK command in |buf| at |offset|.
  void WriteLinkCommand(std::shared_ptr<MsdVsiBuffer> buffer, uint32_t offset, uint32_t prefetch,
                        uint32_t gpu_addr);

  // Writes an EVENT command in |buf| at |offset|. |out_semaphore| will be populated with the
  // semaphore that will be signalled once the interrupt associated with the event occurs.
  static void WriteEventCommand(std::unique_ptr<MsdVsiDevice>& device,
                                std::shared_ptr<MsdVsiContext> context,
                                std::shared_ptr<MsdVsiBuffer> buffer, uint32_t offset,
                                std::unique_ptr<magma::PlatformSemaphore>* out_semaphore);

  void DropDefaultClient() { client_ = nullptr; }

  std::shared_ptr<MsdVsiConnection> default_connection() { return client_->connection; }
  std::shared_ptr<MsdVsiContext> default_context() { return client_->context; }
  std::shared_ptr<AddressSpace> default_address_space() { return client_->address_space; }

  std::unique_ptr<MsdVsiDevice> device_;
  std::unique_ptr<AddressSpaceOwner> address_space_owner_;
  std::unique_ptr<Client> client_;
};
