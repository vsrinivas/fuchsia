// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "helper/platform_device_helper.h"
#include "src/graphics/drivers/msd-vsl-gc/src/command_buffer.h"
#include "src/graphics/drivers/msd-vsl-gc/src/msd_vsl_device.h"

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
    device_ = MsdVslDevice::Create(GetTestDeviceHandle(), false /* start_device_thread */);
    ASSERT_NE(device_, nullptr);
    ASSERT_TRUE(device_->IsIdle());

    address_space_owner_ = std::make_unique<AddressSpaceOwner>(device_->GetBusMapper());
    ASSERT_NO_FATAL_FAILURE(CreateClient(kAddressSpaceIndex, &client_));
  }

 protected:
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
    std::shared_ptr<MsdVslConnection> connection;
    std::shared_ptr<MsdVslContext> context;
    std::shared_ptr<AddressSpace> address_space;

    Client(std::shared_ptr<MsdVslConnection> connection, std::shared_ptr<MsdVslContext> context,
           std::shared_ptr<AddressSpace> address_space)
        : connection(connection), context(context), address_space(address_space) {}
  };

  void CreateClient(uint32_t address_space_index, std::unique_ptr<Client>* out_client) {
    std::shared_ptr<AddressSpace> address_space =
        AddressSpace::Create(address_space_owner_.get(), address_space_index);
    ASSERT_NE(address_space, nullptr);

    device_->page_table_arrays()->AssignAddressSpace(address_space_index, address_space.get());

    auto connection =
        std::make_shared<MsdVslConnection>(device_.get(), address_space, 1 /* client_id */);
    ASSERT_NE(connection, nullptr);
    auto context = std::make_shared<MsdVslContext>(connection, address_space);
    ASSERT_NE(context, nullptr);
    *out_client = std::make_unique<Client>(connection, context, address_space);
  }

  // Creates a buffer of |buffer_size| and stores it in |out_buffer|.
  void CreateMsdBuffer(uint32_t buffer_size, std::shared_ptr<MsdVslBuffer>* out_buffer);

  // Creates a buffer of |buffer_size| bytes, and maps the buffer to |gpu_addr|.
  // |map_page_count| may be less bytes than buffer size.
  void CreateAndMapBuffer(uint32_t buffer_size, uint32_t map_page_count, uint32_t gpu_addr,
                          std::shared_ptr<MsdVslBuffer>* out_buffer);

  // Creates a new command buffer.
  // |data_size| is the actual length of the user provided data and may be smaller than
  // the size of |buffer|.
  // |signal| is an optional semaphore. If present, it will be signalled after the batch
  // is submitted via |SubmitBatch| and execution completes.
  void CreateAndPrepareBatch(std::shared_ptr<MsdVslBuffer> buffer, uint32_t data_size,
                             uint32_t batch_offset,
                             std::shared_ptr<magma::PlatformSemaphore> signal,
                             std::unique_ptr<CommandBuffer>* out_batch);

  // Creates a buffer from |buffer_desc|, writes a test instruction to it and
  // submits it as a command buffer. This will wait for execution to complete.
  // If |out_buffer| is non-null, it will be populated with the created buffer.
  void CreateAndSubmitBuffer(const BufferDesc& buffer_desc,
                             std::shared_ptr<MsdVslBuffer>* out_buffer = nullptr);

  // Writes a single WAIT command in |buf| at |offset|.
  void WriteWaitCommand(std::shared_ptr<MsdVslBuffer> buf, uint32_t offset);

  std::shared_ptr<MsdVslConnection> connection() { return client_->connection; }
  std::shared_ptr<MsdVslContext> context() { return client_->context; }
  std::shared_ptr<AddressSpace> address_space() { return client_->address_space; }

  std::unique_ptr<MsdVslDevice> device_;
  std::unique_ptr<AddressSpaceOwner> address_space_owner_;
  std::unique_ptr<Client> client_;
};
