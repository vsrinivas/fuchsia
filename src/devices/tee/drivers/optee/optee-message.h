// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_TEE_DRIVERS_OPTEE_OPTEE_MESSAGE_H_
#define SRC_DEVICES_TEE_DRIVERS_OPTEE_OPTEE_MESSAGE_H_

#include <lib/fit/result.h>
#include <lib/zx/vmo.h>
#include <zircon/assert.h>

#include <cinttypes>
#include <memory>
#include <type_traits>
#include <utility>

#include <fbl/array.h>
#include <fbl/vector.h>
#include <tee-client-api/tee-client-types.h>

#include "optee-llcpp.h"
#include "optee-smc.h"
#include "optee-util.h"
#include "shared-memory.h"

namespace optee {

// OP-TEE Messages
//
// The majority of data exchange with OP-TEE occurs via OP-TEE messages. These are used in
// conjunction with the OP-TEE SMC Call with Arg function. When that SMC function is invoked,
// OP-TEE will expect a physical pointer to an OP-TEE message to be passed in arguments a1 and a2.
//
// Each message is made up of a header and a variable number of parameters. The relevant fields of
// a message can depend on the command and the context, so these helper classes aim to reduce the
// possibilities of invariant access. For example, in some instances, a field might be an input and
// in others, it might be an output.

struct MessageHeader {
  uint32_t command;
  uint32_t app_function;
  uint32_t session_id;
  uint32_t cancel_id;

  uint32_t unused;
  uint32_t return_code;
  uint32_t return_origin;
  uint32_t num_params;
};

struct MessageParam {
  enum AttributeType : uint64_t {
    kAttributeTypeNone = 0x0,
    kAttributeTypeValueInput = 0x1,
    kAttributeTypeValueOutput = 0x2,
    kAttributeTypeValueInOut = 0x3,
    kAttributeTypeRegMemInput = 0x5,
    kAttributeTypeRegMemOutput = 0x6,
    kAttributeTypeRegMemInOut = 0x7,
    kAttributeTypeTempMemInput = 0x9,
    kAttributeTypeTempMemOutput = 0xa,
    kAttributeTypeTempMemInOut = 0xb,

    kAttributeTypeMeta = 0x100,
    kAttributeTypeFragment = 0x200,
  };

  struct TemporaryMemory {
    uint64_t buffer;
    uint64_t size;
    uint64_t shared_memory_reference;
  };

  struct RegisteredMemory {
    uint64_t offset;
    uint64_t size;
    uint64_t shared_memory_reference;
  };

  union Value {
    struct {
      uint64_t a;
      uint64_t b;
      uint64_t c;
    } generic;
    Uuid::Octets uuid_octets;
    struct {
      uint64_t seconds;
      uint64_t nanoseconds;
    } get_time_specs;
    struct {
      uint64_t memory_type;
      uint64_t memory_size;
    } allocate_memory_specs;
    struct {
      uint64_t memory_type;
      uint64_t memory_id;
    } free_memory_specs;
    struct {
      uint64_t command_number;
      uint64_t object_identifier;
      uint64_t object_offset;
    } file_system_command;
    struct {
      uint64_t identifier;
    } file_system_object;
  };

  uint64_t attribute;
  union {
    TemporaryMemory temporary_memory;
    RegisteredMemory registered_memory;
    Value value;
  } payload;
};

// MessageParamList
//
// MessageParamList is a non-owning view of the parameters in a Message. It is only valid within
// the lifetime of the Message.
class MessageParamList {
 public:
  constexpr MessageParamList() : params_(nullptr), count_(0U) {}

  MessageParamList(MessageParam* params, size_t count) : params_(params), count_(count) {}

  size_t size() const { return count_; }
  MessageParam* get() const { return params_; }

  MessageParam& operator[](size_t i) const {
    ZX_DEBUG_ASSERT(i < count_);
    return params_[i];
  }

  MessageParam* begin() const { return params_; }
  MessageParam* end() const { return &params_[count_]; }

 private:
  MessageParam* params_;
  size_t count_;
};

template <typename PtrType>
class MessageBase {
  static_assert(std::is_same<PtrType, SharedMemory*>::value ||
                    std::is_same<PtrType, std::unique_ptr<SharedMemory>>::value,
                "Template type of MessageBase must be a pointer (raw or smart) to SharedMemory!");

 public:
  using SharedMemoryPtr = PtrType;

  zx_paddr_t paddr() const { return memory_->paddr(); }

 protected:
  static constexpr size_t CalculateSize(size_t num_params) {
    return sizeof(MessageHeader) + (sizeof(MessageParam) * num_params);
  }

  // MessageBase
  //
  // Move constructor for MessageBase.
  MessageBase(MessageBase&& msg) : memory_(std::move(msg.memory_)) { msg.memory_ = nullptr; }

  // Move-only, so explicitly delete copy constructor and copy assignment operator for clarity
  MessageBase(const MessageBase&) = delete;
  MessageBase& operator=(const MessageBase&) = delete;

  explicit MessageBase(SharedMemoryPtr memory) : memory_(std::move(memory)) {
    ZX_DEBUG_ASSERT_MSG(memory_ != nullptr, "Cannot create Message with null backing memory");
  }

  MessageHeader* header() const { return reinterpret_cast<MessageHeader*>(memory_->vaddr()); }

  // TODO(rjascani): Change this to return a reference to make ownership rules clearer
  MessageParamList params() const {
    return MessageParamList(reinterpret_cast<MessageParam*>(header() + 1), header()->num_params);
  }

  SharedMemoryPtr memory_;
};

// Message
//
// A normal message from the rich world (REE).
class Message : public MessageBase<std::unique_ptr<SharedMemory>> {
 public:
  enum Command : uint32_t {
    kOpenSession = 0,
    kInvokeCommand = 1,
    kCloseSession = 2,
    kCancel = 3,
    kRegisterSharedMemory = 4,
    kUnregisterSharedMemory = 5,
  };

  // Message
  //
  // Move constructor for Message. Uses the default implicit implementation.
  Message(Message&&) = default;

  // Move-only, so explicitly delete copy constructor and copy assignment operator for clarity
  Message(const Message&) = delete;
  Message& operator=(const Message&) = delete;

  ~Message() = default;

 protected:
  using MessageBase::MessageBase;  // inherit constructors

  zx_status_t TryInitializeParameters(size_t starting_param_index,
                                      fidl::VectorView<fuchsia_tee::wire::Parameter> parameter_set,
                                      SharedMemoryManager::ClientMemoryPool* temp_memory_pool);
  zx_status_t TryInitializeValue(const fuchsia_tee::wire::Value& value, MessageParam* out_param);
  zx_status_t TryInitializeBuffer(fuchsia_tee::wire::Buffer* buffer,
                                  SharedMemoryManager::ClientMemoryPool* temp_memory_pool,
                                  MessageParam* out_param);

  zx_status_t CreateOutputParameterSet(size_t starting_param_index,
                                       ParameterSet* out_parameter_set);

 private:
  // This nested class is just a container for pairing a vmo with a chunk of shared memory. It
  // can be used to synchronize the user provided buffers with the TEE shared memory.
  class TemporarySharedMemory {
   public:
    explicit TemporarySharedMemory(zx::vmo vmo, uint64_t vmo_offset, size_t size,
                                   std::unique_ptr<SharedMemory>);

    TemporarySharedMemory(TemporarySharedMemory&&) = default;
    TemporarySharedMemory& operator=(TemporarySharedMemory&&) = default;

    uint64_t vmo_offset() const { return vmo_offset_; }
    bool is_valid() const { return vmo_.is_valid() && shared_memory_ != nullptr; }

    zx_status_t SyncToSharedMemory();
    zx_status_t SyncToVmo(size_t actual_size);

    zx_handle_t ReleaseVmo();

   private:
    zx::vmo vmo_;
    uint64_t vmo_offset_;
    size_t size_;
    std::unique_ptr<SharedMemory> shared_memory_;
  };

  Value CreateOutputValueParameter(const MessageParam& optee_param);
  zx_status_t CreateOutputBufferParameter(const MessageParam& optee_param, Buffer* out_buffer);

  fbl::Vector<TemporarySharedMemory> allocated_temp_memory_;
};

// OpenSessionMessage
//
// This OP-TEE message is used to start a session between a client app and trusted app.
class OpenSessionMessage : public Message {
 public:
  static fit::result<OpenSessionMessage, zx_status_t> TryCreate(
      SharedMemoryManager::DriverMemoryPool* message_pool,
      SharedMemoryManager::ClientMemoryPool* temp_memory_pool, const Uuid& trusted_app,
      fidl::VectorView<fuchsia_tee::wire::Parameter> parameter_set);

  // Outputs
  uint32_t session_id() const { return header()->session_id; }
  uint32_t return_code() const { return header()->return_code; }
  uint32_t return_origin() const { return header()->return_origin; }

  zx_status_t CreateOutputParameterSet(ParameterSet* out_parameter_set) {
    return Message::CreateOutputParameterSet(kNumFixedOpenSessionParams, out_parameter_set);
  }

 protected:
  using Message::header;   // make header() protected
  using Message::Message;  // inherit constructors

  static constexpr size_t kNumFixedOpenSessionParams = 2;
  static constexpr size_t kTrustedAppParamIndex = 0;
  static constexpr size_t kClientAppParamIndex = 1;
};

// CloseSessionMessage
//
// This OP-TEE message is used to close an existing open session.
class CloseSessionMessage : public Message {
 public:
  static fit::result<CloseSessionMessage, zx_status_t> TryCreate(
      SharedMemoryManager::DriverMemoryPool* message_pool, uint32_t session_id);

  // Outputs
  uint32_t return_code() const { return header()->return_code; }
  uint32_t return_origin() const { return header()->return_origin; }

 protected:
  using Message::header;   // make header() protected
  using Message::Message;  // inherit constructors

  static constexpr size_t kNumParams = 0;
};

// InvokeCommandMessage
//
// This OP-TEE message is used to invoke a command on a session between client app and trusted app.
class InvokeCommandMessage : public Message {
 public:
  static fit::result<InvokeCommandMessage, zx_status_t> TryCreate(
      SharedMemoryManager::DriverMemoryPool* message_pool,
      SharedMemoryManager::ClientMemoryPool* temp_memory_pool, uint32_t session_id,
      uint32_t command_id, fidl::VectorView<fuchsia_tee::wire::Parameter> parameter_set);

  // Outputs
  uint32_t return_code() const { return header()->return_code; }
  uint32_t return_origin() const { return header()->return_origin; }

  zx_status_t CreateOutputParameterSet(ParameterSet* out_parameter_set) {
    return Message::CreateOutputParameterSet(0, out_parameter_set);
  }

 protected:
  using Message::Message;  // inherit constructors
};

// RpcMessage
//
// A message originating from the trusted world (TEE) specifying the details of a RPC request.
class RpcMessage : public MessageBase<SharedMemory*> {
 public:
  enum Command : uint32_t {
    kLoadTa = 0,
    kAccessReplayProtectedMemoryBlock = 1,
    kAccessFileSystem = 2,
    kGetTime = 3,
    kWaitQueue = 4,
    kSuspend = 5,
    kAllocateMemory = 6,
    kFreeMemory = 7,
    kAccessSqlFileSystem = 8,
    kLoadGprof = 9,
    kPerformSocketIo = 10
  };

  // RpcMessage
  //
  // Move constructor for RpcMessage.
  RpcMessage(RpcMessage&& rpc_msg) : MessageBase(std::move(rpc_msg)) {}

  // Move-only, so explicitly delete copy constructor and copy assignment operator for clarity
  RpcMessage(const RpcMessage&) = delete;
  RpcMessage& operator=(const RpcMessage&) = delete;

  // RpcMessage
  //
  // Attempts to create an instance of an RpcMessage from a backing SharedMemory object.
  //
  // Parameters:
  //  * memory:   A pointer to the SharedMemory object backing the RpcMessage. This pointer must
  //              be non-null and valid.
  static fit::result<RpcMessage, zx_status_t> CreateFromSharedMemory(SharedMemory* memory);

  uint32_t command() const { return header()->command; }

  void set_return_origin(uint32_t return_origin) { header()->return_origin = return_origin; }

  void set_return_code(uint32_t return_code) { header()->return_code = return_code; }

 protected:
  explicit RpcMessage(SharedMemory* memory) : MessageBase(memory) {}
};

// LoadTaRpcMessage
//
// A `RpcMessage` that should be interpreted with the command of loading a trusted application.
class LoadTaRpcMessage : public RpcMessage {
 public:
  // LoadTaRpcMessage
  //
  // Move constructor for `LoadTaRpcMessage`. Uses the default implicit implementation.
  LoadTaRpcMessage(LoadTaRpcMessage&&) = default;

  // LoadTaRpcMessage
  //
  // Attempts to create a `LoadTaRpcMessage` from a moved-in `RpcMessage`.
  static fit::result<LoadTaRpcMessage, zx_status_t> CreateFromRpcMessage(RpcMessage&& rpc_message);

  const Uuid& ta_uuid() const { return ta_uuid_; }

  uint64_t memory_reference_id() const { return mem_id_; }

  size_t memory_reference_size() const { return mem_size_; }

  zx_paddr_t memory_reference_paddr() const { return mem_paddr_; }

  void set_output_ta_size(size_t ta_size) {
    ZX_DEBUG_ASSERT(out_ta_size_ != nullptr);
    *out_ta_size_ = static_cast<uint64_t>(ta_size);
  }

 protected:
  explicit LoadTaRpcMessage(RpcMessage&& rpc_message) : RpcMessage(std::move(rpc_message)) {}

  static constexpr size_t kNumParams = 2;
  static constexpr size_t kUuidParamIndex = 0;
  static constexpr size_t kMemoryReferenceParamIndex = 1;

  Uuid ta_uuid_;
  uint64_t mem_id_;
  size_t mem_size_;
  zx_paddr_t mem_paddr_;
  uint64_t* out_ta_size_;
};

// RpmbRpcMessage
//
// A `RpcMessage` that should be interpreted with the command of accessing RPMB memory block
class RpmbRpcMessage : public RpcMessage {
 public:
  enum RpmbCommand : uint64_t {
    kDataRequest = 0,
    kGetDevInfo = 1,
  };

  // RpmbRpcMessage
  //
  // Move constructor for `RpmbRpcMessage`. Uses the default implicit implementation.
  RpmbRpcMessage(RpmbRpcMessage&&) = default;

  // RpmbRpcMessage
  //
  // Attempts to create a `RpmbRpcMessage` from a moved-in `RpcMessage`.
  static fit::result<RpmbRpcMessage, zx_status_t> CreateFromRpcMessage(RpcMessage&& rpc_message);

  uint64_t tx_memory_reference_id() const { return tx_frame_mem_id_; }

  size_t tx_memory_reference_size() const { return tx_frame_mem_size_; }

  zx_paddr_t tx_memory_reference_paddr() const { return tx_frame_mem_paddr_; }

  uint64_t rx_memory_reference_id() const { return rx_frame_mem_id_; }

  size_t rx_memory_reference_size() const { return rx_frame_mem_size_; }

  zx_paddr_t rx_memory_reference_paddr() const { return rx_frame_mem_paddr_; }

 protected:
  explicit RpmbRpcMessage(RpcMessage&& rpc_message) : RpcMessage(std::move(rpc_message)) {}

  static constexpr size_t kNumParams = 2;
  static constexpr size_t kTxMemoryReferenceParamIndex = 0;
  static constexpr size_t kRxMemoryReferenceParamIndex = 1;

  uint64_t tx_frame_mem_id_;
  size_t tx_frame_mem_size_;
  zx_paddr_t tx_frame_mem_paddr_;

  uint64_t rx_frame_mem_id_;
  size_t rx_frame_mem_size_;
  zx_paddr_t rx_frame_mem_paddr_;
};

// GetTimeRpcMessage
//
// A `RpcMessage` that should be interpreted with the command of getting the current time.
class GetTimeRpcMessage : public RpcMessage {
 public:
  // GetTimeRpcMessage
  //
  // Move constructor for `GetTimeRpcMessage`. Uses the default implicit implementation.
  GetTimeRpcMessage(GetTimeRpcMessage&&) = default;

  // GetTimeRpcMessage
  //
  // Attempts to create a `GetTimeRpcMessage` from a moved-in `RpcMessage`.
  static fit::result<GetTimeRpcMessage, zx_status_t> CreateFromRpcMessage(RpcMessage&& rpc_message);

  void set_output_seconds(uint64_t secs) {
    ZX_DEBUG_ASSERT(out_secs_ != nullptr);
    *out_secs_ = secs;
  }

  void set_output_nanoseconds(uint64_t nanosecs) {
    ZX_DEBUG_ASSERT(out_nanosecs_ != nullptr);
    *out_nanosecs_ = nanosecs;
  }

 protected:
  explicit GetTimeRpcMessage(RpcMessage&& rpc_message) : RpcMessage(std::move(rpc_message)) {}

  static constexpr size_t kNumParams = 1;
  static constexpr size_t kTimeParamIndex = 0;

  uint64_t* out_secs_;
  uint64_t* out_nanosecs_;
};

// AllocateMemoryRpcMessage
//
// A `RpcMessage` that should be interpreted with the command of allocating shared memory.
class AllocateMemoryRpcMessage : public RpcMessage {
 public:
  // AllocateMemoryRpcMessage
  //
  // Move constructor for `AllocateMemoryRpcMessage`. Uses the default implicit implementation.
  AllocateMemoryRpcMessage(AllocateMemoryRpcMessage&&) = default;

  // AllocateMemoryRpcMessage
  //
  // Attempts to create a `AllocateMemoryRpcMessage` from a moved-in `RpcMessage`.
  static fit::result<AllocateMemoryRpcMessage, zx_status_t> CreateFromRpcMessage(
      RpcMessage&& rpc_message);

  SharedMemoryType memory_type() const { return memory_type_; }

  size_t memory_size() const { return static_cast<size_t>(memory_size_); }

  void set_output_memory_size(size_t memory_size) {
    ZX_DEBUG_ASSERT(out_memory_size_ != nullptr);
    *out_memory_size_ = static_cast<uint64_t>(memory_size);
  }

  void set_output_buffer(zx_paddr_t buffer_paddr) {
    ZX_DEBUG_ASSERT(out_memory_buffer_ != nullptr);
    *out_memory_buffer_ = static_cast<uint64_t>(buffer_paddr);
  }

  void set_output_memory_identifier(uint64_t id) { *out_memory_id_ = id; }

 protected:
  explicit AllocateMemoryRpcMessage(RpcMessage&& rpc_message)
      : RpcMessage(std::move(rpc_message)) {}

  static constexpr size_t kNumParams = 1;
  static constexpr size_t kMemorySpecsParamIndex = 0;
  static constexpr size_t kOutputTemporaryMemoryParamIndex = 0;

  SharedMemoryType memory_type_;
  size_t memory_size_;
  uint64_t* out_memory_size_;
  uint64_t* out_memory_buffer_;
  uint64_t* out_memory_id_;
};

// FreeMemoryRpcMessage
//
// A `RpcMessage` that should be interpreted with the command of freeing shared memory.
class FreeMemoryRpcMessage : public RpcMessage {
 public:
  // FreeMemoryRpcMessage
  //
  // Move constructor for `FreeMemoryRpcMessage`. Uses the default implicit implementation.
  FreeMemoryRpcMessage(FreeMemoryRpcMessage&&) = default;

  // FreeMemoryRpcMessage
  //
  // Attempts to create a `FreeMemoryRpcMessage from a moved-in `RpcMessage`.
  static fit::result<FreeMemoryRpcMessage, zx_status_t> CreateFromRpcMessage(
      RpcMessage&& rpc_message);

  SharedMemoryType memory_type() const { return memory_type_; }

  uint64_t memory_identifier() const { return memory_id_; }

 protected:
  explicit FreeMemoryRpcMessage(RpcMessage&& rpc_message) : RpcMessage(std::move(rpc_message)) {}

  static constexpr size_t kNumParams = 1;
  static constexpr size_t kMemorySpecsParamIndex = 0;

  SharedMemoryType memory_type_;
  uint64_t memory_id_;
};

// FileSystemRpcMessage
//
// A `RpcMessage` that should be interpreted with the command of accessing the file system.
class FileSystemRpcMessage : public RpcMessage {
 public:
  enum FileSystemCommand : uint64_t {
    kOpenFile = 0,
    kCreateFile = 1,
    kCloseFile = 2,
    kReadFile = 3,
    kWriteFile = 4,
    kTruncateFile = 5,
    kRemoveFile = 6,
    kRenameFile = 7,
    kOpenDirectory = 8,
    kCloseDirectory = 9,
    kGetNextFileInDirectory = 10
  };

  // FileSystemRpcMessage
  //
  // Move constructor for FileSystemRpcMessage. Uses the default implicit implementation.
  FileSystemRpcMessage(FileSystemRpcMessage&&) = default;

  // FileSystemRpcMessage
  //
  // Attempts to create a `FileSystemRpcMessage` from a moved-in RpcMessage.
  static fit::result<FileSystemRpcMessage, zx_status_t> CreateFromRpcMessage(
      RpcMessage&& rpc_message);

  FileSystemCommand file_system_command() const { return fs_command_; }

 protected:
  explicit FileSystemRpcMessage(RpcMessage&& rpc_message) : RpcMessage(std::move(rpc_message)) {}

  static constexpr size_t kNumFileSystemCommands = 11;
  static constexpr size_t kMinNumParams = 1;
  static constexpr size_t kFileSystemCommandParamIndex = 0;

  FileSystemCommand fs_command_;
};

// OpenFileFileSystemRpcMessage
//
// A `FileSystemRpcMessage` that should be interpreted with the command of opening a file.
class OpenFileFileSystemRpcMessage : public FileSystemRpcMessage {
 public:
  // OpenFileFileSystemRpcMessage
  //
  // Move constructor for `OpenFileFileSystemRpcMessage`. Uses the default implicit
  // implementation.
  OpenFileFileSystemRpcMessage(OpenFileFileSystemRpcMessage&&) = default;

  // OpenFileFileSystemRpcMessage
  //
  // Attempts to create a `OpenFileFileSystemRpcMessage` from a moved-in `FileSystemRpcMessage`.
  static fit::result<OpenFileFileSystemRpcMessage, zx_status_t> CreateFromFsRpcMessage(
      FileSystemRpcMessage&& fs_message);

  uint64_t path_memory_identifier() const { return path_mem_id_; }

  size_t path_memory_size() const { return path_mem_size_; }

  zx_paddr_t path_memory_paddr() const { return path_mem_paddr_; }

  void set_output_file_system_object_identifier(uint64_t object_id) {
    ZX_DEBUG_ASSERT(out_fs_object_id_ != nullptr);
    *out_fs_object_id_ = object_id;
  }

 protected:
  explicit OpenFileFileSystemRpcMessage(FileSystemRpcMessage&& fs_message)
      : FileSystemRpcMessage(std::move(fs_message)) {}

  static constexpr size_t kNumParams = 3;
  static constexpr size_t kPathParamIndex = 1;
  static constexpr size_t kOutFileSystemObjectIdParamIndex = 2;

  uint64_t path_mem_id_;
  size_t path_mem_size_;
  zx_paddr_t path_mem_paddr_;
  uint64_t* out_fs_object_id_;
};

// CreateFileFileSystemRpcMessage
//
// A `FileSystemRpcMessage` that should be interpreted with the command of creating a file.
class CreateFileFileSystemRpcMessage : public FileSystemRpcMessage {
 public:
  // CreateFileFileSystemRpcMessage
  //
  // Move constructor for `CreateFileFileSystemRpcMessage`. Uses the default implicit
  // implementation.
  CreateFileFileSystemRpcMessage(CreateFileFileSystemRpcMessage&&) = default;

  // CreateFileFileSystemRpcMessage
  //
  // Attempts to create a `CreateFileFileSystemRpcMessage` from a moved-in `FileSystemRpcMessage`.
  static fit::result<CreateFileFileSystemRpcMessage, zx_status_t> CreateFromFsRpcMessage(
      FileSystemRpcMessage&& fs_message);

  uint64_t path_memory_identifier() const { return path_mem_id_; }

  size_t path_memory_size() const { return path_mem_size_; }

  zx_paddr_t path_memory_paddr() const { return path_mem_paddr_; }

  void set_output_file_system_object_identifier(uint64_t object_id) {
    ZX_DEBUG_ASSERT(out_fs_object_id_ != nullptr);
    *out_fs_object_id_ = object_id;
  }

 protected:
  explicit CreateFileFileSystemRpcMessage(FileSystemRpcMessage&& fs_message)
      : FileSystemRpcMessage(std::move(fs_message)) {}

  static constexpr size_t kNumParams = 3;
  static constexpr size_t kPathParamIndex = 1;
  static constexpr size_t kOutFileSystemObjectIdParamIndex = 2;

  uint64_t path_mem_id_;
  size_t path_mem_size_;
  zx_paddr_t path_mem_paddr_;
  uint64_t* out_fs_object_id_;
};

// CloseFileFileSystemRpcMessage
//
// A `FileSystemRpcMessage` that should be interpreted with the command of closing a file.
class CloseFileFileSystemRpcMessage : public FileSystemRpcMessage {
 public:
  // CloseFileFileSystemRpcMessage
  //
  // Move constructor for `CloseFileFileSystemRpcMessage`. Uses the default implicit
  // implementation.
  CloseFileFileSystemRpcMessage(CloseFileFileSystemRpcMessage&&) = default;

  // CloseFileFileSystemRpcMessage
  //
  // Attempts to create a `CloseFileFileSystemRpcMessage` from a moved-in `FileSystemRpcMessage`.
  static fit::result<CloseFileFileSystemRpcMessage, zx_status_t> CreateFromFsRpcMessage(
      FileSystemRpcMessage&& fs_message);

  uint64_t file_system_object_identifier() const { return fs_object_id_; }

 protected:
  explicit CloseFileFileSystemRpcMessage(FileSystemRpcMessage&& fs_message)
      : FileSystemRpcMessage(std::move(fs_message)) {}

  static constexpr size_t kNumParams = 1;

  uint64_t fs_object_id_;
};

// ReadFileFileSystemRpcMessage
//
// A `FileSystemRpcMessage` that should be interpreted with the command of reading an open file.
class ReadFileFileSystemRpcMessage : public FileSystemRpcMessage {
 public:
  // ReadFileFileSystemRpcMessage
  //
  // Move constructor for `ReadFileFileSystemRpcMessage`. Uses the default implicit
  // implementation.
  ReadFileFileSystemRpcMessage(ReadFileFileSystemRpcMessage&&) = default;

  // ReadFileFileSystemRpcMessage
  //
  // Attempts to create a `ReadFileFileSystemRpcMessage` from a moved-in `FileSystemRpcMessage`.
  static fit::result<ReadFileFileSystemRpcMessage, zx_status_t> CreateFromFsRpcMessage(
      FileSystemRpcMessage&& fs_message);

  uint64_t file_system_object_identifier() const { return fs_object_id_; }

  uint64_t file_offset() const { return file_offset_; }

  uint64_t file_contents_memory_identifier() const { return file_contents_mem_id_; }

  size_t file_contents_memory_size() const { return file_contents_mem_size_; }

  zx_paddr_t file_contents_memory_paddr() const { return file_contents_mem_paddr_; }

  void set_output_file_contents_size(size_t size) const {
    ZX_DEBUG_ASSERT(out_file_contents_size_ != nullptr);
    *out_file_contents_size_ = static_cast<uint64_t>(size);
  }

 protected:
  explicit ReadFileFileSystemRpcMessage(FileSystemRpcMessage&& fs_message)
      : FileSystemRpcMessage(std::move(fs_message)) {}

  static constexpr size_t kNumParams = 2;
  static constexpr size_t kOutReadBufferMemoryParamIndex = 1;

  uint64_t fs_object_id_;
  uint64_t file_offset_;
  uint64_t file_contents_mem_id_;
  size_t file_contents_mem_size_;
  zx_paddr_t file_contents_mem_paddr_;
  uint64_t* out_file_contents_size_;
};

// WriteFileFileSystemRpcMessage
//
// A `FileSystemRpcMessage` that should be interpreted with the command of writing to an open file.
class WriteFileFileSystemRpcMessage : public FileSystemRpcMessage {
 public:
  // WriteFileFileSystemRpcMessage
  //
  // Move constructor for `WriteFileFileSystemRpcMessage`. Uses the default implicit
  // implementation.
  WriteFileFileSystemRpcMessage(WriteFileFileSystemRpcMessage&&) = default;

  // WriteFileFileSystemRpcMessage
  //
  // Constructs a `WriteFileFileSystemRpcMessage` from a moved-in `FileSystemRpcMessage`.
  static fit::result<WriteFileFileSystemRpcMessage, zx_status_t> CreateFromFsRpcMessage(
      FileSystemRpcMessage&& fs_message);

  uint64_t file_system_object_identifier() const { return fs_object_id_; }

  uint64_t file_offset() const { return file_offset_; }

  uint64_t file_contents_memory_identifier() const { return file_contents_mem_id_; }

  size_t file_contents_memory_size() const { return file_contents_mem_size_; }

  zx_paddr_t file_contents_memory_paddr() const { return file_contents_mem_paddr_; }

 protected:
  explicit WriteFileFileSystemRpcMessage(FileSystemRpcMessage&& fs_message)
      : FileSystemRpcMessage(std::move(fs_message)) {}

  static constexpr size_t kNumParams = 2;
  static constexpr size_t kWriteBufferMemoryParam = 1;

  uint64_t fs_object_id_;
  uint64_t file_offset_;
  uint64_t file_contents_mem_id_;
  size_t file_contents_mem_size_;
  zx_paddr_t file_contents_mem_paddr_;
};

// TruncateFileFileSystemRpcMessage
//
// A `FileSystemRpcMessage` that should be interpreted with the command of truncating a file.
class TruncateFileFileSystemRpcMessage : public FileSystemRpcMessage {
 public:
  // TruncateFileFileSystemRpcMessage
  //
  // Move constructor for `TruncateFileFileSystemRpcMessage`. Uses the default implicit
  // implementation.
  TruncateFileFileSystemRpcMessage(TruncateFileFileSystemRpcMessage&&) = default;

  // TruncateFileFileSystemRpcMessage
  //
  // Attempts to create a `TruncateFileFileSystemRpcMessage` from a moved-in
  // `FileSystemRpcMessage`.
  static fit::result<TruncateFileFileSystemRpcMessage, zx_status_t> CreateFromFsRpcMessage(
      FileSystemRpcMessage&& fs_message);

  uint64_t file_system_object_identifier() const { return fs_object_id_; }

  uint64_t target_file_size() const { return target_file_size_; }

 protected:
  explicit TruncateFileFileSystemRpcMessage(FileSystemRpcMessage&& fs_message)
      : FileSystemRpcMessage(std::move(fs_message)) {}

  static constexpr size_t kNumParams = 1;

  uint64_t fs_object_id_;
  uint64_t target_file_size_;
};

// RemoveFileFileSystemRpcMessage
//
// A `FileSystemRpcMessage` that should be interpreted with the command of removing a file.
class RemoveFileFileSystemRpcMessage : public FileSystemRpcMessage {
 public:
  // RemoveFileFileSystemRpcMessage
  //
  // Move constructor for `RemoveFileFileSystemRpcMessage`. Uses the default implicit
  // implementation.
  RemoveFileFileSystemRpcMessage(RemoveFileFileSystemRpcMessage&&) = default;

  // RemoveFileFileSystemRpcMessage
  //
  // Attempts to create a `RemoveFileFileSystemRpcMessage` from a moved-in `FileSystemRpcMessage`.
  static fit::result<RemoveFileFileSystemRpcMessage, zx_status_t> CreateFromFsRpcMessage(
      FileSystemRpcMessage&& fs_message);

  uint64_t path_memory_identifier() const { return path_mem_id_; }

  size_t path_memory_size() const { return path_mem_size_; }

  zx_paddr_t path_memory_paddr() const { return path_mem_paddr_; }

 protected:
  explicit RemoveFileFileSystemRpcMessage(FileSystemRpcMessage&& fs_message)
      : FileSystemRpcMessage(std::move(fs_message)) {}

  static constexpr size_t kNumParams = 2;
  static constexpr size_t kFileNameParamIndex = 1;

  uint64_t path_mem_id_;
  size_t path_mem_size_;
  zx_paddr_t path_mem_paddr_;
};

// RenameFileFileSystemRpcMessage
//
// A `FileSystemRpcMessage` that should be interpreted with the command of renaming a file.
class RenameFileFileSystemRpcMessage : public FileSystemRpcMessage {
 public:
  // RenameFileFileSystemRpcMessage
  //
  // Move constructor for `RenameFileFileSystemRpcMessage`. Uses the default implicit
  // implementation.
  RenameFileFileSystemRpcMessage(RenameFileFileSystemRpcMessage&&) = default;

  // RenameFileFileSystemRpcMessage
  //
  // Attempts to create a `RenameFileFileSystemRpcMessage` from a moved-in `FileSystemRpcMessage`.
  static fit::result<RenameFileFileSystemRpcMessage, zx_status_t> CreateFromFsRpcMessage(
      FileSystemRpcMessage&& fs_message);

  bool should_overwrite() const { return should_overwrite_; }

  uint64_t old_file_name_memory_identifier() const { return old_file_name_mem_id_; }

  size_t old_file_name_memory_size() const { return old_file_name_mem_size_; }

  zx_paddr_t old_file_name_memory_paddr() const { return old_file_name_mem_paddr_; }

  uint64_t new_file_name_memory_identifier() const { return new_file_name_mem_id_; }

  size_t new_file_name_memory_size() const { return new_file_name_mem_size_; }

  zx_paddr_t new_file_name_memory_paddr() const { return new_file_name_mem_paddr_; }

 protected:
  explicit RenameFileFileSystemRpcMessage(FileSystemRpcMessage&& fs_message)
      : FileSystemRpcMessage(std::move(fs_message)) {}

  static constexpr size_t kNumParams = 3;
  static constexpr size_t kOldFileNameParamIndex = 1;
  static constexpr size_t kNewFileNameParamIndex = 2;

  bool should_overwrite_;
  uint64_t old_file_name_mem_id_;
  size_t old_file_name_mem_size_;
  zx_paddr_t old_file_name_mem_paddr_;
  uint64_t new_file_name_mem_id_;
  size_t new_file_name_mem_size_;
  zx_paddr_t new_file_name_mem_paddr_;
};

}  // namespace optee

#endif  // SRC_DEVICES_TEE_DRIVERS_OPTEE_OPTEE_MESSAGE_H_
