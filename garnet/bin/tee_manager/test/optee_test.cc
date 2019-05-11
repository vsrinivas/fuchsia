// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/tee/cpp/fidl.h>
#include <gtest/gtest.h>
#include <lib/sys/cpp/testing/test_with_environment.h>
#include <src/security/tee/third_party/optee_test/ta_storage.h>
#include <tee-client-api/tee_client_api.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

namespace optee {

namespace test {

namespace {
constexpr uint32_t kPrivateStorage = 0x1;
constexpr uint32_t kFlagRead = 0x1;
constexpr uint32_t kFlagWrite = 0x2;
constexpr uint32_t kFlagWriteMetadata = 0x4;

class OpteeFileHandleGuard {
 public:
  OpteeFileHandleGuard() = default;

  constexpr OpteeFileHandleGuard(TEEC_Session* session, uint32_t handle)
      : session_(session), handle_(handle) {}

  ~OpteeFileHandleGuard() { Close(); }

  OpteeFileHandleGuard(OpteeFileHandleGuard&& other)
      : session_(other.session_), handle_(std::move(other.handle_)) {
    other.session_ = nullptr;
    other.handle_ = std::nullopt;
  }

  OpteeFileHandleGuard& operator=(OpteeFileHandleGuard&& other) {
    if (&other == this) {
      return *this;
    }

    Close();
    if (other.IsValid()) {
      session_ = other.session_;
      handle_ = std::make_optional(other.Release());
    }
    return *this;
  }

  OpteeFileHandleGuard(const OpteeFileHandleGuard&) = delete;
  OpteeFileHandleGuard& operator=(const OpteeFileHandleGuard&) = delete;

  bool IsValid() const { return session_ != nullptr && handle_.has_value(); }

  uint32_t GetHandle() const {
    EXPECT_TRUE(IsValid());
    return *handle_;
  }

  void Close();  // Forward-declare and implement after `CloseFile` definition

  uint32_t Release() {
    EXPECT_TRUE(IsValid());

    uint32_t released = *handle_;
    session_ = nullptr;
    handle_ = std::nullopt;
    return released;
  }

 private:
  TEEC_Session* session_;
  std::optional<uint32_t> handle_;
};

struct OperationResult {
  TEEC_Result result;
  uint32_t return_origin;
};

// Helper class to print numeric values in hex for gtest
template <typename NumericType>
class Hex {
 public:
  constexpr explicit Hex(NumericType number) : number_(number) {}

  friend std::ostream& operator<<(std::ostream& os,
                                  const Hex<NumericType>& obj) {
    return os << "0x" << std::hex << obj.number_;
  }

 private:
  NumericType number_;
};

::testing::AssertionResult IsTeecSuccess(TEEC_Result result) {
  if (result == TEEC_SUCCESS) {
    return ::testing::AssertionSuccess();
  } else {
    return ::testing::AssertionFailure() << "result: " << Hex(result);
  }
}

::testing::AssertionResult IsTeecSuccess(const OperationResult& op_result) {
  if (op_result.result == TEEC_SUCCESS) {
    return ::testing::AssertionSuccess();
  } else {
    return ::testing::AssertionFailure()
           << "result: " << Hex(op_result.result)
           << ", return origin: " << Hex(op_result.return_origin);
  }
}

static std::vector<uint8_t> StringToBuffer(const std::string& s) {
  return std::vector<uint8_t>(s.cbegin(), s.cend());
}

static std::string BufferToString(const std::vector<uint8_t>& buf) {
  return std::string(buf.cbegin(), buf.cend());
}

enum class SeekFrom : uint32_t { kBeginning = 0x0, kCurrent = 0x1, kEnd = 0x2 };

// Invokes the storage TA to create a file. Returns an object handle, if
// successful.
static void CreateFile(TEEC_Session* session, std::string name,
                       std::vector<uint8_t>* init_data, uint32_t flags,
                       OpteeFileHandleGuard* out_handle_guard) {
  ASSERT_NE(session, nullptr);
  ASSERT_NE(init_data, nullptr);
  ASSERT_GT(init_data->size(), 0u)
      << "the trusted application does not support zero-sized initial data";
  ASSERT_NE(out_handle_guard, nullptr);

  TEEC_Operation op = {};
  op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT, TEEC_VALUE_INOUT,
                                   TEEC_VALUE_INPUT, TEEC_MEMREF_TEMP_INPUT);
  op.params[2].value.b = kPrivateStorage;

  op.params[0].tmpref.buffer = static_cast<void*>(name.data());
  op.params[0].tmpref.size = name.size();

  op.params[1].value.a = flags;

  constexpr uint32_t kNullHandle = 0x0;
  op.params[2].value.a = kNullHandle;

  op.params[3].tmpref.buffer = static_cast<void*>(init_data->data());
  op.params[3].tmpref.size = static_cast<uint32_t>(init_data->size());

  OperationResult op_result;
  op_result.result = TEEC_InvokeCommand(session, TA_STORAGE_CMD_CREATE, &op,
                                        &op_result.return_origin);
  ASSERT_TRUE(IsTeecSuccess(op_result));

  *out_handle_guard = OpteeFileHandleGuard(session, op.params[1].value.b);
}

// Invokes the storage TA to open a file. Returns an object handle, if
// successful.
static void OpenFile(TEEC_Session* session, std::string name, uint32_t flags,
                     OpteeFileHandleGuard* out_handle_guard) {
  ASSERT_NE(session, nullptr);
  ASSERT_NE(out_handle_guard, nullptr);

  TEEC_Operation op = {};
  op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT, TEEC_VALUE_INOUT,
                                   TEEC_VALUE_INPUT, TEEC_NONE);
  op.params[2].value.a = kPrivateStorage;

  op.params[0].tmpref.buffer = static_cast<void*>(name.data());
  op.params[0].tmpref.size = name.size();

  op.params[1].value.a = flags;

  OperationResult op_result;
  op_result.result = TEEC_InvokeCommand(session, TA_STORAGE_CMD_OPEN, &op,
                                        &op_result.return_origin);
  ASSERT_TRUE(IsTeecSuccess(op_result));

  *out_handle_guard = OpteeFileHandleGuard(session, op.params[1].value.b);
}

// Invokes the storage TA to close a file.
static void CloseFile(TEEC_Session* session,
                      OpteeFileHandleGuard* handle_guard) {
  ASSERT_NE(session, nullptr);
  ASSERT_NE(handle_guard, nullptr);

  TEEC_Operation op = {};
  op.paramTypes =
      TEEC_PARAM_TYPES(TEEC_VALUE_INPUT, TEEC_NONE, TEEC_NONE, TEEC_NONE);

  op.params[0].value.a = handle_guard->GetHandle();

  OperationResult op_result;
  op_result.result = TEEC_InvokeCommand(session, TA_STORAGE_CMD_CLOSE, &op,
                                        &op_result.return_origin);
  EXPECT_TRUE(IsTeecSuccess(op_result));  // Okay to continue on failure

  handle_guard->Release();
}

// Invokes the storage TA to read from a file. Returns the number of bytes read,
// if successful.
static void ReadFile(TEEC_Session* session,
                     const OpteeFileHandleGuard& handle_guard,
                     std::vector<uint8_t>* buffer) {
  ASSERT_NE(session, nullptr);
  ASSERT_NE(buffer, nullptr);

  TEEC_Operation op = {};
  op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_OUTPUT, TEEC_VALUE_INOUT,
                                   TEEC_NONE, TEEC_NONE);
  op.params[0].tmpref.buffer = static_cast<void*>(buffer->data());
  op.params[0].tmpref.size = static_cast<uint32_t>(buffer->size());

  op.params[1].value.a = handle_guard.GetHandle();

  OperationResult op_result;
  op_result.result = TEEC_InvokeCommand(session, TA_STORAGE_CMD_READ, &op,
                                        &op_result.return_origin);
  ASSERT_TRUE(IsTeecSuccess(op_result));

  size_t bytes = static_cast<size_t>(op.params[1].value.b);
  EXPECT_LE(bytes, buffer->size());

  buffer->resize(bytes);
}

// Invokes the storage TA to write to a file. Returns the number of bytes
// written, if successful.
static void WriteFile(TEEC_Session* session,
                      const OpteeFileHandleGuard& handle_guard,
                      std::vector<uint8_t>* buffer) {
  ASSERT_NE(session, nullptr);
  ASSERT_NE(buffer, nullptr);

  TEEC_Operation op = {};
  op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT, TEEC_VALUE_INPUT,
                                   TEEC_NONE, TEEC_NONE);
  op.params[0].tmpref.buffer = static_cast<void*>(buffer->data());
  op.params[0].tmpref.size = static_cast<uint32_t>(buffer->size());

  op.params[1].value.a = handle_guard.GetHandle();

  OperationResult op_result;
  op_result.result = TEEC_InvokeCommand(session, TA_STORAGE_CMD_WRITE, &op,
                                        &op_result.return_origin);
  EXPECT_TRUE(IsTeecSuccess(op_result));  // Okay to continue on failure
}

// Invokes the storage TA to seek in a file. If successful, returns the offset
// from the beginning of the file.
static void SeekFile(TEEC_Session* session,
                     const OpteeFileHandleGuard& handle_guard, int32_t offset,
                     SeekFrom whence, uint32_t* out_absolute_offset) {
  ASSERT_NE(session, nullptr);

  TEEC_Operation op = {};
  op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_INPUT, TEEC_VALUE_INOUT,
                                   TEEC_NONE, TEEC_NONE);
  op.params[0].value.a = handle_guard.GetHandle();

  // Intentionally copy this int32_t to an uint32_t field, as the TA
  // reinterprets these bits as an int32_t
  static_assert(sizeof(offset) == sizeof(op.params[0].value.b));
  std::memcpy(&op.params[0].value.b, &offset, sizeof(uint32_t));

  op.params[1].value.a = static_cast<uint32_t>(whence);

  OperationResult op_result;
  op_result.result = TEEC_InvokeCommand(session, TA_STORAGE_CMD_SEEK, &op,
                                        &op_result.return_origin);
  ASSERT_TRUE(IsTeecSuccess(op_result));

  // Since there are scenarios where the client may ignore this, check if null
  if (out_absolute_offset != nullptr) {
    *out_absolute_offset = op.params[1].value.b;
  }
}

// Invokes the storage TA to unlink a file.
static void UnlinkFile(TEEC_Session* session,
                       OpteeFileHandleGuard* handle_guard) {
  ASSERT_NE(session, nullptr);
  ASSERT_NE(handle_guard, nullptr);

  TEEC_Operation op = {};
  op.paramTypes =
      TEEC_PARAM_TYPES(TEEC_VALUE_INPUT, TEEC_NONE, TEEC_NONE, TEEC_NONE);
  op.params[0].value.a = handle_guard->GetHandle();

  OperationResult op_result;
  op_result.result = TEEC_InvokeCommand(session, TA_STORAGE_CMD_UNLINK, &op,
                                        &op_result.return_origin);
  EXPECT_TRUE(IsTeecSuccess(op_result));  // Okay to continue on failure

  handle_guard->Release();
}

void OpteeFileHandleGuard::Close() {
  if (IsValid()) {
    CloseFile(session_, this);
    session_ = nullptr;
    handle_ = std::nullopt;
  }
}

class ContextGuard {
 public:
  ContextGuard() : context_(nullptr) {}

  explicit ContextGuard(TEEC_Context* context) : context_(context) {}

  ~ContextGuard() { Close(); }

  ContextGuard(ContextGuard&& other) : context_(other.context_) {
    other.context_ = nullptr;
  }

  ContextGuard& operator=(ContextGuard&& other) {
    if (&other == this) {
      return *this;
    }

    Close();
    context_ = other.Release();
    return *this;
  }

  ContextGuard(const ContextGuard&) = delete;
  ContextGuard& operator=(const ContextGuard&) = delete;

  constexpr bool IsValid() const { return context_ != nullptr; }

  TEEC_Context* Get() const { return context_; }

  void Close() {
    if (IsValid()) {
      TEEC_FinalizeContext(context_);
      context_ = nullptr;
    }
  }

  TEEC_Context* Release() {
    TEEC_Context* released = context_;
    context_ = nullptr;
    return released;
  }

 private:
  TEEC_Context* context_;
};

class SessionGuard {
 public:
  constexpr SessionGuard() : session_(nullptr) {}

  constexpr explicit SessionGuard(TEEC_Session* session) : session_(session) {}

  ~SessionGuard() { Close(); }

  SessionGuard(SessionGuard&& other) : session_(other.session_) {
    other.session_ = nullptr;
  }

  SessionGuard& operator=(SessionGuard&& other) {
    if (&other == this) {
      return *this;
    }

    Close();
    session_ = other.Release();
    return *this;
  }

  SessionGuard(const SessionGuard&) = delete;
  SessionGuard& operator=(const SessionGuard&) = delete;

  constexpr bool IsValid() const { return session_ != nullptr; }

  TEEC_Session* Get() const { return session_; }

  void Close() {
    if (IsValid()) {
      TEEC_CloseSession(session_);
      session_ = nullptr;
    }
  }

  TEEC_Session* Release() {
    TEEC_Session* released = session_;
    session_ = nullptr;
    return released;
  }

 private:
  TEEC_Session* session_;
};

}  // namespace

class OpteeTest : public sys::testing::TestWithEnvironment {
 protected:
  void SetUp() override {
    auto services = CreateServices();

    fuchsia::sys::LaunchInfo launch_info{
        "fuchsia-pkg://fuchsia.com/tee_manager#meta/tee_manager.cmx"};
    zx_status_t status = services->AddServiceWithLaunchInfo(
        std::move(launch_info), fuchsia::tee::Device::Name_);
    ASSERT_EQ(status, ZX_OK);

    environment_ =
        CreateNewEnclosingEnvironment("optee_test", std::move(services));
    WaitForEnclosingEnvToStart(environment_.get());

    TEEC_Result result = TEEC_InitializeContext(nullptr, &context_);
    ASSERT_TRUE(IsTeecSuccess(result));
    context_guard_ = ContextGuard(&context_);

    OperationResult op_result;
    op_result.result =
        TEEC_OpenSession(&context_, &session_, &kStorageUuid, TEEC_LOGIN_PUBLIC,
                         nullptr, nullptr, &op_result.return_origin);
    ASSERT_TRUE(IsTeecSuccess(op_result));
    session_guard_ = SessionGuard(&session_);

    std::vector<uint8_t> buffer = StringToBuffer(GetInitialFileContents());
    OpteeFileHandleGuard handle_guard;
    CreateFile(&session_, GetFileName(), &buffer, kFlagRead, &handle_guard);
  }

  void TearDown() override {
    constexpr uint32_t kOpenFlags = kFlagRead | kFlagWrite | kFlagWriteMetadata;
    OpteeFileHandleGuard handle_guard;
    OpenFile(&session_, GetFileName(), kOpenFlags, &handle_guard);

    UnlinkFile(&session_, &handle_guard);
  }

  static std::string GetFileName() {
    static const std::string kFileName = "optee_test_file";
    return kFileName;
  }

  static std::string GetInitialFileContents() {
    static const std::string kInitialContents =
        "the quick brown fox jumped over the lazy dog";
    return kInitialContents;
  }

  TEEC_Session* GetSession() { return &session_; }

 private:
  static constexpr TEEC_UUID kStorageUuid = TA_STORAGE_UUID;

  std::unique_ptr<sys::testing::EnclosingEnvironment> environment_;
  TEEC_Context context_;
  ContextGuard context_guard_;
  TEEC_Session session_;
  SessionGuard session_guard_;
};

TEST_F(OpteeTest, OpenFile) {
  OpteeFileHandleGuard handle_guard;
  OpenFile(GetSession(), GetFileName(), kFlagRead, &handle_guard);
}

TEST_F(OpteeTest, ReadFile) {
  OpteeFileHandleGuard handle_guard;
  OpenFile(GetSession(), GetFileName(), kFlagRead, &handle_guard);

  constexpr size_t kBufferSize = 128;
  std::vector<uint8_t> buffer(kBufferSize, 0);
  ReadFile(GetSession(), handle_guard, &buffer);

  std::string read_contents = BufferToString(buffer);
  EXPECT_EQ(read_contents, GetInitialFileContents());
}

TEST_F(OpteeTest, WriteFile) {
  constexpr uint32_t kOpenFlags = kFlagRead | kFlagWrite | kFlagWriteMetadata;
  OpteeFileHandleGuard handle_guard;
  OpenFile(GetSession(), GetFileName(), kOpenFlags, &handle_guard);

  static const std::string kNewFileContents(
      "how much wood would a woodchuck chuck if a woodchuck could chuck wood?");
  ASSERT_GE(kNewFileContents.size(), GetInitialFileContents().size());

  std::vector<uint8_t> buffer = StringToBuffer(kNewFileContents);
  WriteFile(GetSession(), handle_guard, &buffer);
}

TEST_F(OpteeTest, WriteAndReadFile) {
  constexpr uint32_t kOpenFlags = kFlagRead | kFlagWrite | kFlagWriteMetadata;
  static const std::string kNewFileContents(
      "how much wood would a woodchuck chuck if a woodchuck could chuck wood?");
  ASSERT_GE(kNewFileContents.size(), GetInitialFileContents().size());
  std::vector<uint8_t> buffer;

  {
    OpteeFileHandleGuard handle_guard;
    OpenFile(GetSession(), GetFileName(), kOpenFlags, &handle_guard);

    buffer = StringToBuffer(kNewFileContents);
    WriteFile(GetSession(), handle_guard, &buffer);
  }

  {
    OpteeFileHandleGuard handle_guard;
    OpenFile(GetSession(), GetFileName(), kOpenFlags, &handle_guard);
    constexpr size_t kBufferSize = 128;
    buffer.assign(kBufferSize, 0);
    ReadFile(GetSession(), handle_guard, &buffer);

    std::string read_contents = BufferToString(buffer);
    EXPECT_EQ(read_contents, kNewFileContents);
  }
}

TEST_F(OpteeTest, SeekWriteReadFile) {
  constexpr uint32_t kOpenFlags = kFlagRead | kFlagWrite | kFlagWriteMetadata;
  static const std::string kStringToAppend = "!";

  OpteeFileHandleGuard handle_guard;
  OpenFile(GetSession(), GetFileName(), kOpenFlags, &handle_guard);

  // Seek to the end of the file
  uint32_t absolute_offset = 0;
  SeekFile(GetSession(), handle_guard, 0, SeekFrom::kEnd, &absolute_offset);
  EXPECT_EQ(absolute_offset, GetInitialFileContents().size());

  // Append an exclamation point to the file
  std::vector<uint8_t> buffer = StringToBuffer(kStringToAppend);
  WriteFile(GetSession(), handle_guard, &buffer);

  // Seek to the beginning of the file
  absolute_offset = std::numeric_limits<decltype(absolute_offset)>::max();
  SeekFile(GetSession(), handle_guard, 0, SeekFrom::kBeginning,
           &absolute_offset);
  EXPECT_EQ(absolute_offset, 0u);

  // Check the new string
  const std::string kNewFileContents =
      GetInitialFileContents() + kStringToAppend;
  constexpr size_t kBufferSize = 128;
  buffer.assign(kBufferSize, 0);  // Zero out and resize the buffer
  ReadFile(GetSession(), handle_guard, &buffer);
  std::string read_contents = BufferToString(buffer);
  EXPECT_EQ(read_contents, kNewFileContents);
}

}  // namespace test
}  // namespace optee