// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <poll.h>

#include <chrono>
#include <thread>

#include "gtest/gtest.h"
#include "platform_connection.h"
#include "platform_connection_client.h"
#ifdef __linux__
#include "linux/linux_platform_connection_client.h"  // nogncheck
#endif

namespace {
constexpr uint32_t kImmediateCommandCount = 128;
// The total size of all commands should not be a multiple of the receive buffer size.
constexpr uint32_t kImmediateCommandSize = 2048 * 3 / 2 / kImmediateCommandCount;

static inline int page_size() { return sysconf(_SC_PAGESIZE); }

}  // namespace

class TestPlatformConnection {
 public:
  static std::unique_ptr<TestPlatformConnection> Create();

  TestPlatformConnection(std::unique_ptr<magma::PlatformConnectionClient> client_connection,
                         std::thread ipc_thread,
                         std::shared_ptr<magma::PlatformConnection> connection)
      : client_connection_(std::move(client_connection)),
        ipc_thread_(std::move(ipc_thread)),
        connection_(std::move(connection)) {}

  ~TestPlatformConnection() {
    client_connection_.reset();
    connection_.reset();
    if (ipc_thread_.joinable())
      ipc_thread_.join();
    EXPECT_TRUE(test_complete);
  }

  void TestImportBuffer() {
    auto buf = magma::PlatformBuffer::Create(1, "test");
    test_buffer_id = buf->id();
    EXPECT_EQ(client_connection_->ImportBuffer(buf.get()), 0);
    EXPECT_EQ(client_connection_->GetError(), 0);
  }
  void TestReleaseBuffer() {
    auto buf = magma::PlatformBuffer::Create(1, "test");
    test_buffer_id = buf->id();
    EXPECT_EQ(client_connection_->ImportBuffer(buf.get()), 0);
    EXPECT_EQ(client_connection_->ReleaseBuffer(test_buffer_id), 0);
    EXPECT_EQ(client_connection_->GetError(), 0);
  }

  void TestImportObject() {
    auto semaphore = magma::PlatformSemaphore::Create();
    ASSERT_TRUE(semaphore);
    test_semaphore_id = semaphore->id();
    uint32_t handle;
    EXPECT_TRUE(semaphore->duplicate_handle(&handle));
    EXPECT_EQ(client_connection_->ImportObject(handle, magma::PlatformObject::SEMAPHORE), 0);
    EXPECT_EQ(client_connection_->GetError(), 0);
  }
  void TestReleaseObject() {
    auto semaphore = magma::PlatformSemaphore::Create();
    ASSERT_TRUE(semaphore);
    test_semaphore_id = semaphore->id();
    uint32_t handle;
    EXPECT_TRUE(semaphore->duplicate_handle(&handle));
    EXPECT_EQ(client_connection_->ImportObject(handle, magma::PlatformObject::SEMAPHORE), 0);
    EXPECT_EQ(
        client_connection_->ReleaseObject(test_semaphore_id, magma::PlatformObject::SEMAPHORE), 0);
    EXPECT_EQ(client_connection_->GetError(), 0);
  }

  void TestCreateContext() {
    uint32_t context_id;
    client_connection_->CreateContext(&context_id);
    EXPECT_EQ(client_connection_->GetError(), 0);
    EXPECT_EQ(test_context_id, context_id);
  }
  void TestDestroyContext() {
    client_connection_->DestroyContext(test_context_id);
    EXPECT_EQ(client_connection_->GetError(), 0);
  }

  void TestExecuteCommandBufferWithResources() {
    ASSERT_EQ(TestPlatformConnection::test_command_buffer.num_resources,
              TestPlatformConnection::test_resources.size());
    ASSERT_EQ(TestPlatformConnection::test_command_buffer.wait_semaphore_count +
                  TestPlatformConnection::test_command_buffer.signal_semaphore_count,
              TestPlatformConnection::test_semaphores.size());
    client_connection_->ExecuteCommandBufferWithResources(
        test_context_id, &test_command_buffer, test_resources.data(), test_semaphores.data());
    EXPECT_EQ(client_connection_->GetError(), 0);
  }

  void TestGetError() {
    EXPECT_EQ(client_connection_->GetError(), 0);
    test_complete = true;
  }

  void TestMapUnmapBuffer() {
    auto buf = magma::PlatformBuffer::Create(1, "test");
    test_buffer_id = buf->id();
    EXPECT_EQ(client_connection_->ImportBuffer(buf.get()), 0);
    EXPECT_EQ(client_connection_->MapBufferGpu(buf->id(), page_size() * 1000, 1u, 2u, 5), 0);
    EXPECT_EQ(client_connection_->UnmapBufferGpu(buf->id(), page_size() * 1000), 0);
    EXPECT_EQ(client_connection_->CommitBuffer(buf->id(), 1000, 2000), 0);
    EXPECT_EQ(client_connection_->GetError(), 0);
  }

  void TestNotificationChannel() {
    constexpr uint64_t kFiveSecondsInNs = 5000000000;
    magma_status_t status = client_connection_->WaitNotificationChannel(kFiveSecondsInNs);
    ASSERT_EQ(MAGMA_STATUS_OK, status);

    uint32_t out_data;
    uint64_t out_data_size;
    // Data was written when the channel was created, so it should be
    // available.
    status =
        client_connection_->ReadNotificationChannel(&out_data, sizeof(out_data), &out_data_size);
    EXPECT_EQ(MAGMA_STATUS_OK, status);
    EXPECT_EQ(sizeof(out_data), out_data_size);
    EXPECT_EQ(5u, out_data);

    // No more data to read.
    status = client_connection_->WaitNotificationChannel(0);
    EXPECT_EQ(MAGMA_STATUS_TIMED_OUT, status);

    status =
        client_connection_->ReadNotificationChannel(&out_data, sizeof(out_data), &out_data_size);
    EXPECT_EQ(MAGMA_STATUS_OK, status);
    EXPECT_EQ(0u, out_data_size);

    // Shutdown other end of pipe.
    connection_->ShutdownEvent()->Signal();
    connection_.reset();
    ipc_thread_.join();
    EXPECT_TRUE(got_null_notification);

    // Poll should still terminate early.
    status = client_connection_->WaitNotificationChannel(kFiveSecondsInNs);
    EXPECT_EQ(MAGMA_STATUS_CONNECTION_LOST, status);

    status =
        client_connection_->ReadNotificationChannel(&out_data, sizeof(out_data), &out_data_size);
    EXPECT_EQ(MAGMA_STATUS_CONNECTION_LOST, status);
    test_complete = true;
  }

  void TestExecuteImmediateCommands() {
    uint8_t commands_buffer[kImmediateCommandSize * kImmediateCommandCount] = {};
    uint64_t semaphore_ids[]{0, 1, 2};
    magma_inline_command_buffer commands[kImmediateCommandCount];
    for (size_t i = 0; i < kImmediateCommandCount; i++) {
      commands[i].data = commands_buffer;
      commands[i].size = kImmediateCommandSize;
      commands[i].semaphore_count = 3;
      commands[i].semaphore_ids = semaphore_ids;
    }

    client_connection_->ExecuteImmediateCommands(test_context_id, kImmediateCommandCount, commands);
    EXPECT_EQ(client_connection_->GetError(), 0);
  }

  void TestMultipleGetError() {
    std::vector<std::thread> threads;
    for (uint32_t i = 0; i < 1000; i++) {
      threads.push_back(
          std::thread([this]() { EXPECT_EQ(MAGMA_STATUS_OK, client_connection_->GetError()); }));
    }

    for (auto& thread : threads) {
      thread.join();
    }
    test_complete = true;
  }

  static uint64_t test_buffer_id;
  static uint32_t test_context_id;
  static uint64_t test_semaphore_id;
  static bool got_null_notification;
  static magma_status_t test_error;
  static bool test_complete;
  static std::unique_ptr<magma::PlatformSemaphore> test_semaphore;
  static std::vector<magma_system_exec_resource> test_resources;
  static std::vector<uint64_t> test_semaphores;
  static magma_system_command_buffer test_command_buffer;

 private:
  static void IpcThreadFunc(std::shared_ptr<magma::PlatformConnection> connection) {
    magma::PlatformConnection::RunLoop(connection);
  }

  std::unique_ptr<magma::PlatformConnectionClient> client_connection_;
  std::thread ipc_thread_;
  std::shared_ptr<magma::PlatformConnection> connection_;
};

uint64_t TestPlatformConnection::test_buffer_id;
uint64_t TestPlatformConnection::test_semaphore_id;
uint32_t TestPlatformConnection::test_context_id;
magma_status_t TestPlatformConnection::test_error;
bool TestPlatformConnection::test_complete;
std::unique_ptr<magma::PlatformSemaphore> TestPlatformConnection::test_semaphore;
bool TestPlatformConnection::got_null_notification;
std::vector<magma_system_exec_resource> TestPlatformConnection::test_resources = {
    {.buffer_id = 10, .offset = 11, .length = 12}, {.buffer_id = 13, .offset = 14, .length = 15}};
std::vector<uint64_t> TestPlatformConnection::test_semaphores = {{1000, 1001, 1002}};
magma_system_command_buffer TestPlatformConnection::test_command_buffer = {
    .num_resources = 2,
    .wait_semaphore_count = 2,
    .signal_semaphore_count = 1,
};

class TestDelegate : public magma::PlatformConnection::Delegate {
 public:
  bool ImportBuffer(uint32_t handle, uint64_t* buffer_id_out) override {
    auto buf = magma::PlatformBuffer::Import(handle);
    EXPECT_EQ(buf->id(), TestPlatformConnection::test_buffer_id);
    TestPlatformConnection::test_complete = true;
    return true;
  }
  bool ReleaseBuffer(uint64_t buffer_id) override {
    EXPECT_EQ(buffer_id, TestPlatformConnection::test_buffer_id);
    TestPlatformConnection::test_complete = true;
    return true;
  }

  bool ImportObject(uint32_t handle, magma::PlatformObject::Type object_type) override {
    auto semaphore = magma::PlatformSemaphore::Import(handle);
    if (!semaphore)
      return false;
    EXPECT_EQ(semaphore->id(), TestPlatformConnection::test_semaphore_id);
    TestPlatformConnection::test_complete = true;
    return true;
  }
  bool ReleaseObject(uint64_t object_id, magma::PlatformObject::Type object_type) override {
    EXPECT_EQ(object_id, TestPlatformConnection::test_semaphore_id);
    TestPlatformConnection::test_complete = true;
    return true;
  }

  bool CreateContext(uint32_t context_id) override {
    TestPlatformConnection::test_context_id = context_id;
    TestPlatformConnection::test_complete = true;
    return true;
  }
  bool DestroyContext(uint32_t context_id) override {
    EXPECT_EQ(context_id, TestPlatformConnection::test_context_id);
    TestPlatformConnection::test_complete = true;
    return true;
  }

  magma::Status ExecuteCommandBufferWithResources(
      uint32_t context_id, std::unique_ptr<magma_system_command_buffer> command_buffer,
      std::vector<magma_system_exec_resource> resources,
      std::vector<uint64_t> semaphores) override {
    EXPECT_EQ(context_id, TestPlatformConnection::test_context_id);
    EXPECT_EQ(0, memcmp(command_buffer.get(), &TestPlatformConnection::test_command_buffer,
                        sizeof(magma_system_command_buffer)));
    EXPECT_EQ(0, memcmp(resources.data(), TestPlatformConnection::test_resources.data(),
                        TestPlatformConnection::test_resources.size() *
                            sizeof(TestPlatformConnection::test_resources[0])));
    EXPECT_EQ(0, memcmp(semaphores.data(), TestPlatformConnection::test_semaphores.data(),
                        TestPlatformConnection::test_semaphores.size() *
                            sizeof(TestPlatformConnection::test_semaphores[0])));
    TestPlatformConnection::test_complete = true;
    return MAGMA_STATUS_OK;
  }

  bool MapBufferGpu(uint64_t buffer_id, uint64_t gpu_va, uint64_t page_offset, uint64_t page_count,
                    uint64_t flags) override {
    EXPECT_EQ(TestPlatformConnection::test_buffer_id, buffer_id);
    EXPECT_EQ(page_size() * 1000lu, gpu_va);
    EXPECT_EQ(1u, page_offset);
    EXPECT_EQ(2u, page_count);
    EXPECT_EQ(5u, flags);
    return true;
  }

  bool UnmapBufferGpu(uint64_t buffer_id, uint64_t gpu_va) override {
    EXPECT_EQ(TestPlatformConnection::test_buffer_id, buffer_id);
    EXPECT_EQ(page_size() * 1000lu, gpu_va);
    return true;
  }

  bool CommitBuffer(uint64_t buffer_id, uint64_t page_offset, uint64_t page_count) override {
    EXPECT_EQ(TestPlatformConnection::test_buffer_id, buffer_id);
    EXPECT_EQ(1000lu, page_offset);
    EXPECT_EQ(2000lu, page_count);
    return true;
  }

  void SetNotificationCallback(msd_connection_notification_callback_t callback,
                               void* token) override {
    if (!token) {
      // This doesn't count as test complete because it should happen in every test when the
      // server shuts down.
      TestPlatformConnection::got_null_notification = true;
    } else {
      uint32_t data = 5;
      msd_notification_t n = {.type = MSD_CONNECTION_NOTIFICATION_CHANNEL_SEND};
      *reinterpret_cast<uint32_t*>(n.u.channel_send.data) = data;
      n.u.channel_send.size = sizeof(data);
      callback(token, &n);
    }
  }

  magma::Status ExecuteImmediateCommands(uint32_t context_id, uint64_t commands_size,
                                         void* commands, uint64_t semaphore_count,
                                         uint64_t* semaphores) override {
    EXPECT_GE(2048u, commands_size);
    uint8_t received_bytes[2048] = {};
    EXPECT_EQ(0, memcmp(received_bytes, commands, commands_size));
    uint64_t command_count = commands_size / kImmediateCommandSize;
    EXPECT_EQ(3u * command_count, semaphore_count);
    for (uint32_t i = 0; i < command_count; i++) {
      EXPECT_EQ(0u, semaphores[0]);
      EXPECT_EQ(1u, semaphores[1]);
      EXPECT_EQ(2u, semaphores[2]);
      semaphores += 3;
    }
    immediate_commands_bytes_executed_ += commands_size;
    TestPlatformConnection::test_complete =
        immediate_commands_bytes_executed_ == kImmediateCommandSize * kImmediateCommandCount;

    // Also check thread name
    EXPECT_EQ("ConnectionThread 1", magma::PlatformThreadHelper::GetCurrentThreadName());

    return MAGMA_STATUS_OK;
  }

  uint64_t immediate_commands_bytes_executed_ = 0;
};

std::unique_ptr<TestPlatformConnection> TestPlatformConnection::Create() {
  test_buffer_id = 0xcafecafecafecafe;
  test_semaphore_id = ~0u;
  test_context_id = 0xdeadbeef;
  test_error = 0x12345678;
  test_complete = false;
  got_null_notification = false;
  auto delegate = std::make_unique<TestDelegate>();

  std::unique_ptr<magma::PlatformConnectionClient> client_connection;
#ifdef __linux__
  // Using in-process connection
  client_connection = std::make_unique<magma::LinuxPlatformConnectionClient>(delegate.get());
#endif

  auto connection = magma::PlatformConnection::Create(std::move(delegate), 1u);
  if (!connection)
    return DRETP(nullptr, "failed to create PlatformConnection");

  if (!client_connection) {
    client_connection = magma::PlatformConnectionClient::Create(
        connection->GetClientEndpoint(), connection->GetClientNotificationEndpoint());
  }

  if (!client_connection)
    return DRETP(nullptr, "failed to create PlatformConnectionClient");

  auto ipc_thread = std::thread(IpcThreadFunc, connection);

  return std::make_unique<TestPlatformConnection>(std::move(client_connection),
                                                  std::move(ipc_thread), connection);
}

TEST(PlatformConnection, GetError) {
  auto Test = TestPlatformConnection::Create();
  ASSERT_NE(Test, nullptr);
  Test->TestGetError();
}

TEST(PlatformConnection, ImportBuffer) {
  auto Test = TestPlatformConnection::Create();
  ASSERT_NE(Test, nullptr);
  Test->TestImportBuffer();
}

TEST(PlatformConnection, ReleaseBuffer) {
  auto Test = TestPlatformConnection::Create();
  ASSERT_NE(Test, nullptr);
  Test->TestReleaseBuffer();
}

TEST(PlatformConnection, ImportObject) {
  auto Test = TestPlatformConnection::Create();
  ASSERT_NE(Test, nullptr);
  Test->TestImportObject();
}

TEST(PlatformConnection, ReleaseObject) {
  auto Test = TestPlatformConnection::Create();
  ASSERT_NE(Test, nullptr);
  Test->TestReleaseObject();
}

TEST(PlatformConnection, CreateContext) {
  auto Test = TestPlatformConnection::Create();
  ASSERT_NE(Test, nullptr);
  Test->TestCreateContext();
}

TEST(PlatformConnection, DestroyContext) {
  auto Test = TestPlatformConnection::Create();
  ASSERT_NE(Test, nullptr);
  Test->TestDestroyContext();
}

TEST(PlatformConnection, ExecuteCommandBufferWithResources) {
  auto Test = TestPlatformConnection::Create();
  ASSERT_NE(Test, nullptr);
  Test->TestExecuteCommandBufferWithResources();
}

TEST(PlatformConnection, MapUnmapBuffer) {
  auto Test = TestPlatformConnection::Create();
  ASSERT_NE(Test, nullptr);
  Test->TestMapUnmapBuffer();
}

TEST(PlatformConnection, NotificationChannel) {
  auto Test = TestPlatformConnection::Create();
  ASSERT_NE(Test, nullptr);
  Test->TestNotificationChannel();
}

TEST(PlatformConnection, ExecuteImmediateCommands) {
  auto Test = TestPlatformConnection::Create();
  ASSERT_NE(Test, nullptr);
  Test->TestExecuteImmediateCommands();
}

TEST(PlatformConnection, MultipleGetError) {
  auto Test = TestPlatformConnection::Create();
  ASSERT_NE(Test, nullptr);
  Test->TestMultipleGetError();
}
