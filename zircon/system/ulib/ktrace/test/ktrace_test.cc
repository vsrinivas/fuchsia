#include <fuchsia/tracing/kernel/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/ktrace/ktrace.h>
#include <lib/zircon-internal/ktrace.h>
#include <zircon/assert.h>

#include <vector>

#include <zxtest/zxtest.h>

namespace {

class FakeSysCalls {
 public:
  static constexpr zx_handle_t kFakeRootHandle = 0xAABBCCDD;

  internal::KTraceSysCalls Interface() {
    return {
        .ktrace_control =
            [this](zx_handle_t handle, uint32_t action, uint32_t options, void* ptr) {
              latest_action = action;
              root_handle = handle;
              ktrace_control_called = true;
              return ZX_OK;
            },
        .ktrace_read =
            [this](zx_handle_t handle, void* data, uint32_t offset, size_t data_size,
                   size_t* actual) {
              root_handle = handle;
              ktrace_read_called = true;
              size_t copy_len = 0;
              if (offset < read_bytes.size()) {
                copy_len = std::min(data_size, read_bytes.size() - offset);
                memcpy(data, read_bytes.data() + offset, copy_len);
              }
              *actual = copy_len;
              return ZX_OK;
            },
    };
  }

  void CheckControlCall(uint32_t action) {
    EXPECT_TRUE(ktrace_control_called);
    EXPECT_EQ(root_handle, kFakeRootHandle);
    EXPECT_EQ(action, latest_action);
    Reset();
  }

  void CheckReadCall(uint8_t* bytes, size_t length, uint32_t offset) {
    EXPECT_TRUE(ktrace_read_called);
    EXPECT_EQ(root_handle, kFakeRootHandle);
    ASSERT_LE(offset, read_bytes.size());
    ASSERT_LE(length, read_bytes.size() - offset);
    if (bytes) {
      EXPECT_BYTES_EQ(bytes, read_bytes.data() + offset, length);
    }
    Reset();
  }

  void Reset() {
    latest_action = 0;
    root_handle = 0;
    ktrace_read_called = false;
    ktrace_control_called = false;
  }

  uint32_t latest_action;
  zx_handle_t root_handle;
  std::vector<uint8_t> read_bytes = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E};
  bool ktrace_control_called;
  bool ktrace_read_called;
};

class KTraceTest : public zxtest::Test {
 public:
  FakeSysCalls& syscall() { return sys_calls_; }

 protected:
  void SetUp() override {
    loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
    ZX_ASSERT(ZX_OK == loop_->StartThread("KTraceTestLoop"));
    context_ = reinterpret_cast<void*>(FakeSysCalls::kFakeRootHandle);
    ASSERT_OK(ktrace_get_service_provider()->ops->init(&context_));

    // Set fake syscalls
    internal::OverrideKTraceSysCall(context_, sys_calls_.Interface());

    // Connect service channel
    zx::channel controller, reader;
    ASSERT_OK(zx::channel::create(0, &controller, &controller_service));
    ASSERT_OK(zx::channel::create(0, &reader, &reader_service));
    ASSERT_OK(ktrace_get_service_provider()->ops->connect(
        context_, loop_->dispatcher(), ktrace_get_service_provider()->services[0],
        controller.release()));
    ASSERT_OK(ktrace_get_service_provider()->ops->connect(
        context_, loop_->dispatcher(), ktrace_get_service_provider()->services[1],
        reader.release()));
  }

  void TearDown() override {
    loop_ = nullptr;
    ktrace_get_service_provider()->ops->release(context_);
  }

  zx::channel controller_service;
  zx::channel reader_service;

 private:
  FakeSysCalls sys_calls_;
  void* context_;

  std::unique_ptr<async::Loop> loop_;
};

}  // namespace

TEST_F(KTraceTest, Start) {
  fuchsia::tracing::kernel::Controller_SyncProxy controller(std::move(controller_service));
  zx_status_t call_status;

  EXPECT_OK(controller.Start(0, &call_status));
  EXPECT_OK(call_status);
  syscall().CheckControlCall(KTRACE_ACTION_START);
}

TEST_F(KTraceTest, Stop) {
  fuchsia::tracing::kernel::Controller_SyncProxy controller(std::move(controller_service));
  zx_status_t call_status;

  EXPECT_OK(controller.Stop(&call_status));
  EXPECT_OK(call_status);
  syscall().CheckControlCall(KTRACE_ACTION_STOP);
}

TEST_F(KTraceTest, Rewind) {
  fuchsia::tracing::kernel::Controller_SyncProxy controller(std::move(controller_service));
  zx_status_t call_status;

  EXPECT_OK(controller.Rewind(&call_status));
  EXPECT_OK(call_status);
  syscall().CheckControlCall(KTRACE_ACTION_REWIND);
}

TEST_F(KTraceTest, GetBytesWritten) {
  fuchsia::tracing::kernel::Reader_SyncProxy reader(std::move(reader_service));
  zx_status_t call_status;
  size_t len;

  EXPECT_OK(reader.GetBytesWritten(&call_status, &len));
  EXPECT_OK(call_status);
  syscall().CheckReadCall(nullptr, len, 0);
}

TEST_F(KTraceTest, ReadAll) {
  fuchsia::tracing::kernel::Reader_SyncProxy reader(std::move(reader_service));
  zx_status_t call_status;
  std::vector<uint8_t> buf;

  // Read all the contents
  EXPECT_OK(reader.ReadAt(1024, 0, &call_status, &buf));
  EXPECT_OK(call_status);
  syscall().CheckReadCall(buf.data(), buf.size(), 0);
}

TEST_F(KTraceTest, ReadAtOffset) {
  fuchsia::tracing::kernel::Reader_SyncProxy reader(std::move(reader_service));
  zx_status_t call_status;
  std::vector<uint8_t> buf;

  // Read at a offset
  EXPECT_OK(reader.ReadAt(1024, 2, &call_status, &buf));
  EXPECT_OK(call_status);
  syscall().CheckReadCall(buf.data(), buf.size(), 2);
}

TEST_F(KTraceTest, ReadOutOfBounds) {
  fuchsia::tracing::kernel::Reader_SyncProxy reader(std::move(reader_service));
  zx_status_t call_status;
  std::vector<uint8_t> buf;

  // Read beyond the ktrace buffer
  EXPECT_OK(reader.ReadAt(1024, 1024, &call_status, &buf));
  EXPECT_OK(call_status);
  EXPECT_EQ(buf.size(), 0);
  syscall().CheckReadCall(nullptr, buf.size(), 0);
}
