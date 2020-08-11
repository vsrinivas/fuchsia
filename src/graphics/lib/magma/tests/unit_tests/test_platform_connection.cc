// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <poll.h>

#include <chrono>
#include <mutex>
#include <thread>

#include <gtest/gtest.h>

#include "platform_connection.h"
#include "platform_connection_client.h"

#if defined(__Fuchsia__)
#include "zircon/zircon_platform_connection_client.h"  // nogncheck
#elif defined(__linux__)
#include "linux/linux_platform_connection_client.h"  // nogncheck
#endif

namespace {
constexpr uint32_t kImmediateCommandCount = 128;
// The total size of all commands should not be a multiple of the receive buffer size.
constexpr uint32_t kImmediateCommandSize = 2048 * 3 / 2 / kImmediateCommandCount;

static inline int page_size() { return sysconf(_SC_PAGESIZE); }

}  // namespace

// Included by TestPlatformConnection; validates that each test checks for flow control.
// Since flow control values are written by the server (IPC) thread and read by the main
// test thread, we lock the shared data mutex to ensure safety of memory accesses.
namespace magma {
class FlowControlChecker {
 public:
  FlowControlChecker(std::shared_ptr<magma::PlatformConnection> connection,
                     std::shared_ptr<magma::PlatformConnectionClient> client_connection)
      : connection_(connection), client_connection_(client_connection) {}

  ~FlowControlChecker() {
    if (!flow_control_skipped_) {
      EXPECT_TRUE(flow_control_checked_);
    }
  }

  void Init(std::mutex& mutex) {
    std::unique_lock<std::mutex> lock(mutex);
    std::tie(messages_consumed_start_, bytes_imported_start_) = connection_->GetFlowControlCounts();
    std::tie(messages_inflight_start_, bytes_inflight_start_) =
        client_connection_->GetFlowControlCounts();
  }

  void Release() {
    connection_.reset();
    client_connection_.reset();
  }

  void Check(uint64_t messages, uint64_t bytes, std::mutex& mutex) {
    std::unique_lock<std::mutex> lock(mutex);
    auto [messages_consumed, bytes_imported] = connection_->GetFlowControlCounts();
    EXPECT_EQ(messages_consumed_start_ + messages, messages_consumed);
    EXPECT_EQ(bytes_imported_start_ + bytes, bytes_imported);

    auto [messages_inflight, bytes_inflight] = client_connection_->GetFlowControlCounts();
    EXPECT_EQ(messages_inflight_start_ + messages, messages_inflight);
    EXPECT_EQ(bytes_inflight_start_ + bytes, bytes_inflight);
    flow_control_checked_ = true;
  }

  void Skip() {
    flow_control_skipped_ = true;
    Release();
  }

  std::shared_ptr<magma::PlatformConnection> connection_;
  std::shared_ptr<magma::PlatformConnectionClient> client_connection_;
  bool flow_control_checked_ = false;
  bool flow_control_skipped_ = false;
  // Server
  uint64_t messages_consumed_start_ = 0;
  uint64_t bytes_imported_start_ = 0;
  // Client
  uint64_t messages_inflight_start_ = 0;
  uint64_t bytes_inflight_start_ = 0;
};
}  // namespace magma

struct SharedData {
  // This mutex is useed to ensure safety of multi-threaded updates.
  std::mutex mutex;
  uint64_t test_buffer_id = 0xcafecafecafecafe;
  uint32_t test_context_id = 0xdeadbeef;
  uint64_t test_semaphore_id = ~0u;
  bool got_null_notification = false;
  magma_status_t test_error = 0x12345678;
  bool test_complete = false;
  std::unique_ptr<magma::PlatformSemaphore> test_semaphore;
  std::vector<magma_system_exec_resource> test_resources = {
      {.buffer_id = 10, .offset = 11, .length = 12}, {.buffer_id = 13, .offset = 14, .length = 15}};
  std::vector<uint64_t> test_semaphores = {{1000, 1001, 1010, 1011, 1012}};
  magma_system_command_buffer test_command_buffer = {
      .num_resources = 2,
      .wait_semaphore_count = 2,
      .signal_semaphore_count = 3,
  };
  std::unique_ptr<magma::PlatformHandle> test_access_token;
  bool can_access_performance_counters;
  uint64_t pool_id = UINT64_MAX;
};

static SharedData shared_data;

// Most tests here execute the client commands in the test thread context,
// with a separate server thread processing the commands.
class TestPlatformConnection {
 public:
  // Defaults should avoid tests hitting flow control
  static std::unique_ptr<TestPlatformConnection> Create(uint64_t max_inflight_messages = 1000u,
                                                        uint64_t max_inflight_bytes = 1000000u);

  TestPlatformConnection(std::shared_ptr<magma::PlatformConnectionClient> client_connection,
                         std::thread ipc_thread,
                         std::shared_ptr<magma::PlatformConnection> connection)
      : client_connection_(client_connection),
        ipc_thread_(std::move(ipc_thread)),
        connection_(connection),
        flow_control_checker_(connection, client_connection) {}

  ~TestPlatformConnection() {
    flow_control_checker_.Release();
    client_connection_.reset();
    connection_.reset();
    if (ipc_thread_.joinable())
      ipc_thread_.join();

    EXPECT_TRUE(shared_data.test_complete);
  }

  // Should be called after any shared data initialization.
  void FlowControlInit() { flow_control_checker_.Init(shared_data.mutex); }

  // Should be called before test checks for shared data writes.
  void FlowControlCheck(uint64_t messages, uint64_t bytes) {
    flow_control_checker_.Check(messages, bytes, shared_data.mutex);
  }

  void FlowControlCheckOneMessage() { FlowControlCheck(1, 0); }
  void FlowControlSkip() { flow_control_checker_.Skip(); }

  void TestImportBuffer() {
    auto buf = magma::PlatformBuffer::Create(page_size() * 3, "test");
    shared_data.test_buffer_id = buf->id();
    FlowControlInit();

    EXPECT_EQ(client_connection_->ImportBuffer(buf.get()), 0);
    EXPECT_EQ(client_connection_->GetError(), 0);
    FlowControlCheck(1, buf->size());
  }

  void TestReleaseBuffer() {
    auto buf = magma::PlatformBuffer::Create(1, "test");
    shared_data.test_buffer_id = buf->id();
    FlowControlInit();

    EXPECT_EQ(client_connection_->ImportBuffer(buf.get()), 0);
    EXPECT_EQ(client_connection_->ReleaseBuffer(shared_data.test_buffer_id), 0);
    EXPECT_EQ(client_connection_->GetError(), 0);
    FlowControlCheck(2, buf->size());
  }

  void TestImportObject() {
    auto semaphore = magma::PlatformSemaphore::Create();
    ASSERT_TRUE(semaphore);
    shared_data.test_semaphore_id = semaphore->id();
    FlowControlInit();

    uint32_t handle;
    EXPECT_TRUE(semaphore->duplicate_handle(&handle));
    EXPECT_EQ(client_connection_->ImportObject(handle, magma::PlatformObject::SEMAPHORE), 0);
    EXPECT_EQ(client_connection_->GetError(), 0);
    FlowControlCheckOneMessage();
  }

  void TestReleaseObject() {
    auto semaphore = magma::PlatformSemaphore::Create();
    ASSERT_TRUE(semaphore);
    shared_data.test_semaphore_id = semaphore->id();
    FlowControlInit();

    uint32_t handle;
    EXPECT_TRUE(semaphore->duplicate_handle(&handle));
    EXPECT_EQ(client_connection_->ImportObject(handle, magma::PlatformObject::SEMAPHORE), 0);
    EXPECT_EQ(client_connection_->ReleaseObject(shared_data.test_semaphore_id,
                                                magma::PlatformObject::SEMAPHORE),
              0);
    EXPECT_EQ(client_connection_->GetError(), 0);
    FlowControlCheck(2, 0);
  }

  void TestCreateContext() {
    FlowControlInit();
    uint32_t context_id;
    client_connection_->CreateContext(&context_id);
    EXPECT_EQ(client_connection_->GetError(), 0);
    FlowControlCheckOneMessage();
    EXPECT_EQ(shared_data.test_context_id, context_id);
  }

  void TestDestroyContext() {
    FlowControlInit();
    client_connection_->DestroyContext(shared_data.test_context_id);
    EXPECT_EQ(client_connection_->GetError(), 0);
    FlowControlCheckOneMessage();
  }

  void TestExecuteCommandBufferWithResources() {
    ASSERT_EQ(shared_data.test_command_buffer.num_resources, shared_data.test_resources.size());
    ASSERT_EQ(shared_data.test_command_buffer.wait_semaphore_count +
                  shared_data.test_command_buffer.signal_semaphore_count,
              shared_data.test_semaphores.size());
    FlowControlInit();

    client_connection_->ExecuteCommandBufferWithResources(
        shared_data.test_context_id, &shared_data.test_command_buffer,
        shared_data.test_resources.data(), shared_data.test_semaphores.data());
    EXPECT_EQ(client_connection_->GetError(), 0);
    FlowControlCheckOneMessage();
  }

  void TestGetError() {
    FlowControlSkip();
    EXPECT_EQ(client_connection_->GetError(), 0);
    shared_data.test_complete = true;
  }

  void TestMapUnmapBuffer() {
    auto buf = magma::PlatformBuffer::Create(1, "test");
    shared_data.test_buffer_id = buf->id();
    FlowControlInit();

    EXPECT_EQ(client_connection_->ImportBuffer(buf.get()), 0);
    EXPECT_EQ(client_connection_->MapBufferGpu(buf->id(), page_size() * 1000, 1u, 2u, 5), 0);
    EXPECT_EQ(client_connection_->UnmapBufferGpu(buf->id(), page_size() * 1000), 0);
    EXPECT_EQ(client_connection_->CommitBuffer(buf->id(), 1000, 2000), 0);
    EXPECT_EQ(client_connection_->GetError(), 0);
    FlowControlCheck(4, buf->size());
  }

  void TestNotificationChannel() {
    FlowControlSkip();

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
    EXPECT_TRUE(shared_data.got_null_notification);

    // Poll should still terminate early.
    status = client_connection_->WaitNotificationChannel(kFiveSecondsInNs);
    EXPECT_EQ(MAGMA_STATUS_CONNECTION_LOST, status);

    status =
        client_connection_->ReadNotificationChannel(&out_data, sizeof(out_data), &out_data_size);
    EXPECT_EQ(MAGMA_STATUS_CONNECTION_LOST, status);
    shared_data.test_complete = true;
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
    FlowControlInit();

    uint64_t messages_sent = 0;
    client_connection_->ExecuteImmediateCommands(shared_data.test_context_id,
                                                 kImmediateCommandCount, commands, &messages_sent);
    EXPECT_EQ(client_connection_->GetError(), 0);
    FlowControlCheck(messages_sent, 0);
  }

  void TestMultipleGetError() {
    FlowControlSkip();

    std::vector<std::thread> threads;
    for (uint32_t i = 0; i < 1000; i++) {
      threads.push_back(
          std::thread([this]() { EXPECT_EQ(MAGMA_STATUS_OK, client_connection_->GetError()); }));
    }

    for (auto& thread : threads) {
      thread.join();
    }
    shared_data.test_complete = true;
  }

  void TestEnablePerformanceCounters() {
    FlowControlSkip();

    bool enabled = false;
    EXPECT_EQ(MAGMA_STATUS_OK, client_connection_->IsPerformanceCounterAccessEnabled(&enabled));
    EXPECT_FALSE(enabled);

    {
      std::unique_lock<std::mutex> lock(shared_data.mutex);
      shared_data.can_access_performance_counters = true;
    }

    EXPECT_EQ(MAGMA_STATUS_OK, client_connection_->IsPerformanceCounterAccessEnabled(&enabled));
    EXPECT_TRUE(enabled);

    auto semaphore = magma::PlatformSemaphore::Create();
    uint32_t handle;
    EXPECT_TRUE(semaphore->duplicate_handle(&handle));
    EXPECT_EQ(MAGMA_STATUS_OK,
              client_connection_->AccessPerformanceCounters(magma::PlatformHandle::Create(handle)));

    EXPECT_EQ(client_connection_->GetError(), 0);
    {
      std::unique_lock<std::mutex> lock(shared_data.mutex);
      EXPECT_EQ(shared_data.test_access_token->GetId(), semaphore->id());
    }
  }

  void TestPerformanceCounters() {
    FlowControlInit();
    uint32_t trigger_id;
    uint64_t buffer_id;
    uint32_t buffer_offset;
    uint64_t time;
    uint32_t result_flags;
    uint64_t counter = 2;
    EXPECT_EQ(MAGMA_STATUS_OK, client_connection_->EnablePerformanceCounters(&counter, 1).get());
    std::unique_ptr<magma::PlatformPerfCountPoolClient> pool;
    EXPECT_EQ(MAGMA_STATUS_OK, client_connection_->CreatePerformanceCounterBufferPool(&pool).get());

    EXPECT_EQ(client_connection_->GetError(), MAGMA_STATUS_OK);

    // The GetError() above should wait until the performance counter completion event sent in
    // CreatePerformanceCounterBufferPool is sent and therefore readable.
    {
      std::lock_guard<std::mutex> lock(shared_data.mutex);
      EXPECT_EQ(shared_data.pool_id, pool->pool_id());
    }
    EXPECT_EQ(MAGMA_STATUS_OK,
              pool->ReadPerformanceCounterCompletion(&trigger_id, &buffer_id, &buffer_offset, &time,
                                                     &result_flags)
                  .get());
    EXPECT_EQ(1u, trigger_id);
    EXPECT_EQ(2u, buffer_id);
    EXPECT_EQ(3u, buffer_offset);
    EXPECT_EQ(4u, time);
    EXPECT_EQ(1u, result_flags);

    EXPECT_EQ(MAGMA_STATUS_OK, client_connection_->ReleasePerformanceCounterBufferPool(1).get());
    magma_buffer_offset offset = {2, 3, 4};
    EXPECT_EQ(MAGMA_STATUS_OK,
              client_connection_->AddPerformanceCounterBufferOffsetsToPool(1, &offset, 1).get());
    EXPECT_EQ(MAGMA_STATUS_OK,
              client_connection_->RemovePerformanceCounterBufferFromPool(1, 2).get());
    EXPECT_EQ(MAGMA_STATUS_OK, client_connection_->ClearPerformanceCounters(&counter, 1).get());
    EXPECT_EQ(MAGMA_STATUS_OK, client_connection_->DumpPerformanceCounters(1, 2).get());
    EXPECT_EQ(client_connection_->GetError(), MAGMA_STATUS_OK);

    // The CreatePerformanceCounterBufferPool implementation threw away the server side, so the
    // client should be able to detect that.
    EXPECT_EQ(MAGMA_STATUS_CONNECTION_LOST,
              pool->ReadPerformanceCounterCompletion(&trigger_id, &buffer_id, &buffer_offset, &time,
                                                     &result_flags)
                  .get());
    EXPECT_EQ(client_connection_->GetError(), MAGMA_STATUS_OK);
    FlowControlCheck(7, 0);
  }

 private:
  static void IpcThreadFunc(std::shared_ptr<magma::PlatformConnection> connection) {
    magma::PlatformConnection::RunLoop(connection);
  }

  std::shared_ptr<magma::PlatformConnectionClient> client_connection_;
  std::thread ipc_thread_;
  std::shared_ptr<magma::PlatformConnection> connection_;
  magma::FlowControlChecker flow_control_checker_;
};

class TestDelegate : public magma::PlatformConnection::Delegate {
 public:
  bool ImportBuffer(uint32_t handle, uint64_t* buffer_id_out) override {
    std::unique_lock<std::mutex> lock(shared_data.mutex);
    auto buf = magma::PlatformBuffer::Import(handle);
    EXPECT_EQ(buf->id(), shared_data.test_buffer_id);
    shared_data.test_complete = true;
    return true;
  }
  bool ReleaseBuffer(uint64_t buffer_id) override {
    std::unique_lock<std::mutex> lock(shared_data.mutex);
    EXPECT_EQ(buffer_id, shared_data.test_buffer_id);
    shared_data.test_complete = true;
    return true;
  }

  bool ImportObject(uint32_t handle, magma::PlatformObject::Type object_type) override {
    std::unique_lock<std::mutex> lock(shared_data.mutex);
    auto semaphore = magma::PlatformSemaphore::Import(handle);
    if (!semaphore)
      return false;
    EXPECT_EQ(semaphore->id(), shared_data.test_semaphore_id);
    shared_data.test_complete = true;
    return true;
  }
  bool ReleaseObject(uint64_t object_id, magma::PlatformObject::Type object_type) override {
    std::unique_lock<std::mutex> lock(shared_data.mutex);
    EXPECT_EQ(object_id, shared_data.test_semaphore_id);
    shared_data.test_complete = true;
    return true;
  }

  bool CreateContext(uint32_t context_id) override {
    std::unique_lock<std::mutex> lock(shared_data.mutex);
    shared_data.test_context_id = context_id;
    shared_data.test_complete = true;
    return true;
  }
  bool DestroyContext(uint32_t context_id) override {
    std::unique_lock<std::mutex> lock(shared_data.mutex);
    EXPECT_EQ(context_id, shared_data.test_context_id);
    shared_data.test_complete = true;
    return true;
  }

  magma::Status ExecuteCommandBufferWithResources(
      uint32_t context_id, std::unique_ptr<magma_system_command_buffer> command_buffer,
      std::vector<magma_system_exec_resource> resources,
      std::vector<uint64_t> semaphores) override {
    std::unique_lock<std::mutex> lock(shared_data.mutex);
    EXPECT_EQ(context_id, shared_data.test_context_id);
    EXPECT_EQ(0, memcmp(command_buffer.get(), &shared_data.test_command_buffer,
                        sizeof(magma_system_command_buffer)));
    EXPECT_EQ(0, memcmp(resources.data(), shared_data.test_resources.data(),
                        shared_data.test_resources.size() * sizeof(shared_data.test_resources[0])));
    EXPECT_EQ(0,
              memcmp(semaphores.data(), shared_data.test_semaphores.data(),
                     shared_data.test_semaphores.size() * sizeof(shared_data.test_semaphores[0])));
    shared_data.test_complete = true;
    return MAGMA_STATUS_OK;
  }

  bool MapBufferGpu(uint64_t buffer_id, uint64_t gpu_va, uint64_t page_offset, uint64_t page_count,
                    uint64_t flags) override {
    std::unique_lock<std::mutex> lock(shared_data.mutex);
    EXPECT_EQ(shared_data.test_buffer_id, buffer_id);
    EXPECT_EQ(page_size() * 1000lu, gpu_va);
    EXPECT_EQ(1u, page_offset);
    EXPECT_EQ(2u, page_count);
    EXPECT_EQ(5u, flags);
    return true;
  }

  bool UnmapBufferGpu(uint64_t buffer_id, uint64_t gpu_va) override {
    std::unique_lock<std::mutex> lock(shared_data.mutex);
    EXPECT_EQ(shared_data.test_buffer_id, buffer_id);
    EXPECT_EQ(page_size() * 1000lu, gpu_va);
    return true;
  }

  bool CommitBuffer(uint64_t buffer_id, uint64_t page_offset, uint64_t page_count) override {
    std::unique_lock<std::mutex> lock(shared_data.mutex);
    EXPECT_EQ(shared_data.test_buffer_id, buffer_id);
    EXPECT_EQ(1000lu, page_offset);
    EXPECT_EQ(2000lu, page_count);
    return true;
  }

  void SetNotificationCallback(msd_connection_notification_callback_t callback,
                               void* token) override {
    if (!token) {
      std::unique_lock<std::mutex> lock(shared_data.mutex);
      // This doesn't count as test complete because it should happen in every test when the
      // server shuts down.
      shared_data.got_null_notification = true;
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
    std::unique_lock<std::mutex> lock(shared_data.mutex);
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
    shared_data.test_complete =
        immediate_commands_bytes_executed_ == kImmediateCommandSize * kImmediateCommandCount;

    // Also check thread name
    EXPECT_EQ("ConnectionThread 1", magma::PlatformThreadHelper::GetCurrentThreadName());

    return MAGMA_STATUS_OK;
  }

  magma::Status AccessPerformanceCounters(std::unique_ptr<magma::PlatformHandle> event) override {
    std::unique_lock<std::mutex> lock(shared_data.mutex);
    shared_data.test_access_token = std::move(event);
    shared_data.test_complete = true;
    return MAGMA_STATUS_OK;
  }

  bool IsPerformanceCounterAccessEnabled() override {
    std::unique_lock<std::mutex> lock(shared_data.mutex);
    return shared_data.can_access_performance_counters;
  }

  magma::Status EnablePerformanceCounters(const uint64_t* counters,
                                          uint64_t counter_count) override {
    EXPECT_EQ(counter_count, 1u);
    EXPECT_EQ(2u, counters[0]);

    return MAGMA_STATUS_OK;
  }

  magma::Status CreatePerformanceCounterBufferPool(
      std::unique_ptr<magma::PlatformPerfCountPool> pool) override {
    std::unique_lock<std::mutex> lock(shared_data.mutex);
    shared_data.pool_id = pool->pool_id();
    constexpr uint32_t kTriggerId = 1;
    constexpr uint64_t kBufferId = 2;
    constexpr uint32_t kBufferOffset = 3;
    constexpr uint64_t kTimestamp = 4;
    constexpr uint64_t kResultFlags = 1;

    EXPECT_EQ(MAGMA_STATUS_OK,
              pool->SendPerformanceCounterCompletion(kTriggerId, kBufferId, kBufferOffset,
                                                     kTimestamp, kResultFlags)
                  .get());
    return MAGMA_STATUS_OK;
  }

  magma::Status ReleasePerformanceCounterBufferPool(uint64_t pool_id) override {
    EXPECT_EQ(1u, pool_id);
    return MAGMA_STATUS_OK;
  }

  magma::Status AddPerformanceCounterBufferOffsetToPool(uint64_t pool_id, uint64_t buffer_id,
                                                        uint64_t buffer_offset,
                                                        uint64_t buffer_size) override {
    EXPECT_EQ(1u, pool_id);
    EXPECT_EQ(2u, buffer_id);
    EXPECT_EQ(3u, buffer_offset);
    EXPECT_EQ(4u, buffer_size);
    return MAGMA_STATUS_OK;
  }

  magma::Status RemovePerformanceCounterBufferFromPool(uint64_t pool_id,
                                                       uint64_t buffer_id) override {
    EXPECT_EQ(1u, pool_id);
    EXPECT_EQ(2u, buffer_id);
    return MAGMA_STATUS_OK;
  }

  magma::Status DumpPerformanceCounters(uint64_t pool_id, uint32_t trigger_id) override {
    EXPECT_EQ(1u, pool_id);
    EXPECT_EQ(2u, trigger_id);
    std::unique_lock<std::mutex> lock(shared_data.mutex);
    shared_data.test_complete = true;

    return MAGMA_STATUS_OK;
  }

  magma::Status ClearPerformanceCounters(const uint64_t* counters,
                                         uint64_t counter_count) override {
    EXPECT_EQ(1u, counter_count);
    EXPECT_EQ(2u, counters[0]);
    return MAGMA_STATUS_OK;
  }

  uint64_t immediate_commands_bytes_executed_ = 0;

  magma::PlatformConnection* connection_;
};

std::unique_ptr<TestPlatformConnection> TestPlatformConnection::Create(
    uint64_t max_inflight_messages, uint64_t max_inflight_bytes) {
  auto delegate = std::make_unique<TestDelegate>();

  std::shared_ptr<magma::PlatformConnectionClient> client_connection;
#ifdef __linux__
  // Using in-process connection
  client_connection = std::make_unique<magma::LinuxPlatformConnectionClient>(delegate.get());
#endif

  TestDelegate* delegate_ptr = delegate.get();

  auto connection =
      magma::PlatformConnection::Create(std::move(delegate), 1u, /*thread_profile*/ nullptr);
  if (!connection)
    return DRETP(nullptr, "failed to create PlatformConnection");

  delegate_ptr->connection_ = connection.get();

  if (!client_connection) {
    client_connection = magma::PlatformConnectionClient::Create(
        connection->GetClientEndpoint(), connection->GetClientNotificationEndpoint(),
        max_inflight_messages, max_inflight_bytes);
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

TEST(PlatformConnection, EnablePerformanceCounters) {
  auto Test = TestPlatformConnection::Create();
  ASSERT_NE(Test, nullptr);
  Test->TestEnablePerformanceCounters();
}

TEST(PlatformConnection, PrimaryWrapperFlowControlWithoutBytes) {
#ifdef __Fuchsia__
  constexpr uint64_t kMaxMessages = 10;
  constexpr uint64_t kMaxBytes = 10;
  {
    magma::PrimaryWrapper wrapper(zx::channel(ZX_HANDLE_INVALID), kMaxMessages, kMaxBytes);
    auto [wait, count, bytes] = wrapper.ShouldWait(0);
    EXPECT_FALSE(wait);
    EXPECT_EQ(1u, count);
    EXPECT_EQ(0u, bytes);
  }
  {
    magma::PrimaryWrapper wrapper(zx::channel(ZX_HANDLE_INVALID), kMaxMessages, kMaxBytes);
    constexpr uint64_t kStartMessages = 9;
    wrapper.set_for_test(kStartMessages, 0);
    auto [wait, count, bytes] = wrapper.ShouldWait(0);
    EXPECT_FALSE(wait);
    EXPECT_EQ(kStartMessages + 1, count);
    EXPECT_EQ(0u, bytes);
  }
  {
    magma::PrimaryWrapper wrapper(zx::channel(ZX_HANDLE_INVALID), kMaxMessages, kMaxBytes);
    constexpr uint64_t kStartMessages = 10;
    wrapper.set_for_test(kStartMessages, 0);
    auto [wait, count, bytes] = wrapper.ShouldWait(0);
    EXPECT_TRUE(wait);
    EXPECT_EQ(kStartMessages + 1, count);
    EXPECT_EQ(0u, bytes);
  }
#else
  GTEST_SKIP();
#endif
}

TEST(PlatformConnection, PrimaryWrapperFlowControlWithBytes) {
#ifdef __Fuchsia__
  constexpr uint64_t kMaxMessages = 10;
  constexpr uint64_t kMaxBytes = 10;
  {
    magma::PrimaryWrapper wrapper(zx::channel(ZX_HANDLE_INVALID), kMaxMessages, kMaxBytes);
    constexpr uint64_t kNewBytes = 5;
    auto [wait, count, bytes] = wrapper.ShouldWait(kNewBytes);
    EXPECT_FALSE(wait);
    EXPECT_EQ(1u, count);
    EXPECT_EQ(kNewBytes, bytes);
  }
  {
    magma::PrimaryWrapper wrapper(zx::channel(ZX_HANDLE_INVALID), kMaxMessages, kMaxBytes);
    constexpr uint64_t kNewBytes = 15;
    auto [wait, count, bytes] = wrapper.ShouldWait(kNewBytes);
    EXPECT_FALSE(wait);  // Limit exceeded ok, we can pass a single message of any size
    EXPECT_EQ(1u, count);
    EXPECT_EQ(kNewBytes, bytes);
  }
  {
    magma::PrimaryWrapper wrapper(zx::channel(ZX_HANDLE_INVALID), kMaxMessages, kMaxBytes);
    constexpr uint64_t kStartBytes = 4;
    constexpr uint64_t kNewBytes = 10;
    wrapper.set_for_test(0, kStartBytes);
    auto [wait, count, bytes] = wrapper.ShouldWait(kNewBytes);
    EXPECT_FALSE(wait);  // Limit exceeded ok, we're at less than half byte limit
    EXPECT_EQ(1u, count);
    EXPECT_EQ(kStartBytes + kNewBytes, bytes);
  }
  {
    magma::PrimaryWrapper wrapper(zx::channel(ZX_HANDLE_INVALID), kMaxMessages, kMaxBytes);
    constexpr uint64_t kStartBytes = 5;
    constexpr uint64_t kNewBytes = 5;
    wrapper.set_for_test(0, 5);
    auto [wait, count, bytes] = wrapper.ShouldWait(kNewBytes);
    EXPECT_FALSE(wait);
    EXPECT_EQ(1u, count);
    EXPECT_EQ(kStartBytes + kNewBytes, bytes);
  }
  {
    magma::PrimaryWrapper wrapper(zx::channel(ZX_HANDLE_INVALID), kMaxMessages, kMaxBytes);
    constexpr uint64_t kStartBytes = 5;
    constexpr uint64_t kNewBytes = 6;
    wrapper.set_for_test(0, kStartBytes);
    auto [wait, count, bytes] = wrapper.ShouldWait(kNewBytes);
    EXPECT_TRUE(wait);
    EXPECT_EQ(1u, count);
    EXPECT_EQ(kStartBytes + kNewBytes, bytes);
  }
  {
    magma::PrimaryWrapper wrapper(zx::channel(ZX_HANDLE_INVALID), kMaxMessages, kMaxBytes);
    constexpr uint64_t kStartBytes = kMaxBytes;
    constexpr uint64_t kNewBytes = 0;
    wrapper.set_for_test(0, kStartBytes);
    auto [wait, count, bytes] = wrapper.ShouldWait(kNewBytes);
    EXPECT_FALSE(wait);  // At max bytes, not sending more
    EXPECT_EQ(1u, count);
    EXPECT_EQ(kStartBytes + kNewBytes, bytes);
  }
  {
    magma::PrimaryWrapper wrapper(zx::channel(ZX_HANDLE_INVALID), kMaxMessages, kMaxBytes);
    constexpr uint64_t kStartBytes = kMaxBytes + 1;
    constexpr uint64_t kNewBytes = 0;
    wrapper.set_for_test(0, kStartBytes);
    auto [wait, count, bytes] = wrapper.ShouldWait(kNewBytes);
    EXPECT_FALSE(wait);  // Above max bytes, not sending more
    EXPECT_EQ(1u, count);
    EXPECT_EQ(kStartBytes + kNewBytes, bytes);
  }
  {
    magma::PrimaryWrapper wrapper(zx::channel(ZX_HANDLE_INVALID), kMaxMessages, kMaxBytes);
    constexpr uint64_t kStartBytes = kMaxBytes;
    constexpr uint64_t kNewBytes = 1;
    wrapper.set_for_test(0, kStartBytes);
    auto [wait, count, bytes] = wrapper.ShouldWait(kNewBytes);
    EXPECT_TRUE(wait);  // At max bytes, sending more
    EXPECT_EQ(1u, count);
    EXPECT_EQ(kStartBytes + kNewBytes, bytes);
  }
  {
    magma::PrimaryWrapper wrapper(zx::channel(ZX_HANDLE_INVALID), kMaxMessages, kMaxBytes);
    constexpr uint64_t kStartBytes = kMaxBytes + 1;
    constexpr uint64_t kNewBytes = 1;
    wrapper.set_for_test(0, kStartBytes);
    auto [wait, count, bytes] = wrapper.ShouldWait(kNewBytes);
    EXPECT_TRUE(wait);  // Above max bytes, sending more
    EXPECT_EQ(1u, count);
    EXPECT_EQ(kStartBytes + kNewBytes, bytes);
  }
#else
  GTEST_SKIP();
#endif
}

TEST(PlatformConnection, TestPerformanceCounters) {
  auto Test = TestPlatformConnection::Create();
  ASSERT_NE(Test, nullptr);
  Test->TestPerformanceCounters();
}
