// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "optee-client.h"

#include <endian.h>
#include <fuchsia/hardware/rpmb/cpp/banjo.h>
#include <fuchsia/hardware/rpmb/llcpp/fidl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <fuchsia/tee/manager/llcpp/fidl.h>
#include <lib/fidl-utils/bind.h>
#include <lib/fidl/coding.h>
#include <lib/fidl/llcpp/string_view.h>
#include <lib/fidl/llcpp/transaction.h>
#include <lib/fidl/llcpp/vector_view.h>
#include <lib/zx/channel.h>
#include <lib/zx/clock.h>
#include <lib/zx/handle.h>
#include <lib/zx/vmo.h>
#include <libgen.h>
#include <stdlib.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include <algorithm>
#include <ctime>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <ddk/debug.h>
#include <ddktl/fidl.h>
#include <fbl/string_buffer.h>
#include <tee-client-api/tee-client-types.h>

#include "optee-rpmb.h"
#include "optee-smc.h"
#include "optee-util.h"

namespace {

constexpr const char kTaFileExtension[] = ".ta";

// The length of a path to a trusted app consists of its UUID and file extension
// Subtracting 1 from sizeof(char[])s to account for the terminating null character.
constexpr size_t kTaPathLength = optee::Uuid::kUuidStringLength + (sizeof(kTaFileExtension) - 1u);

template <typename SRC_T, typename DST_T>
static constexpr
    typename std::enable_if<std::is_unsigned<SRC_T>::value && std::is_unsigned<DST_T>::value>::type
    SplitInto32BitParts(SRC_T src, DST_T* dst_hi, DST_T* dst_lo) {
  static_assert(sizeof(SRC_T) == 8, "Type SRC_T should be 64 bits!");
  static_assert(sizeof(DST_T) >= 4, "Type DST_T should be at least 32 bits!");
  ZX_DEBUG_ASSERT(dst_hi != nullptr);
  ZX_DEBUG_ASSERT(dst_lo != nullptr);
  *dst_hi = static_cast<DST_T>(src >> 32);
  *dst_lo = static_cast<DST_T>(static_cast<uint32_t>(src));
}

template <typename SRC_T, typename DST_T>
static constexpr
    typename std::enable_if<std::is_unsigned<SRC_T>::value && std::is_unsigned<DST_T>::value>::type
    JoinFrom32BitParts(SRC_T src_hi, SRC_T src_lo, DST_T* dst) {
  static_assert(sizeof(SRC_T) >= 4, "Type SRC_T should be at least 32 bits!");
  static_assert(sizeof(DST_T) >= 8, "Type DST_T should be at least 64-bits!");
  ZX_DEBUG_ASSERT(dst != nullptr);
  *dst = (static_cast<DST_T>(src_hi) << 32) | static_cast<DST_T>(static_cast<uint32_t>(src_lo));
}

// Builds the expected path to a trusted application, formatting the file name per the RFC 4122
// specification.
static fbl::StringBuffer<kTaPathLength> BuildTaPath(const optee::Uuid& ta_uuid) {
  fbl::StringBuffer<kTaPathLength> buf;
  const fbl::String str = ta_uuid.ToString();
  buf.Append(str);
  buf.Append(kTaFileExtension);

  return buf;
}

static zx_status_t ConvertOpteeToZxResult(fidl::AnyAllocator& allocator, uint32_t optee_return_code,
                                          uint32_t optee_return_origin,
                                          fuchsia_tee::wire ::OpResult* zx_result) {
  ZX_DEBUG_ASSERT(zx_result != nullptr);

  // Do a quick check of the return origin to make sure we can map it to one
  // of our FIDL values. If none match, return a communication error instead.
  switch (optee_return_origin) {
    case TEEC_ORIGIN_COMMS:
      zx_result->set_return_code(allocator, optee_return_code);
      zx_result->set_return_origin(allocator, fuchsia_tee::wire::ReturnOrigin::COMMUNICATION);
      break;
    case TEEC_ORIGIN_TEE:
      zx_result->set_return_code(allocator, optee_return_code);
      zx_result->set_return_origin(allocator, fuchsia_tee::wire::ReturnOrigin::TRUSTED_OS);
      break;
    case TEEC_ORIGIN_TRUSTED_APP:
      zx_result->set_return_code(allocator, optee_return_code);
      zx_result->set_return_origin(allocator, fuchsia_tee::wire::ReturnOrigin::TRUSTED_APPLICATION);
      break;
    default:
      LOG(ERROR, "optee: returned an invalid return origin (%" PRIu32 ")", optee_return_origin);
      zx_result->set_return_code(allocator, TEEC_ERROR_COMMUNICATION);
      zx_result->set_return_origin(allocator, fuchsia_tee::wire::ReturnOrigin::COMMUNICATION);
      return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

static std::filesystem::path GetPathFromRawMemory(void* mem, size_t max_size) {
  ZX_DEBUG_ASSERT(mem != nullptr);
  ZX_DEBUG_ASSERT(max_size > 0);

  auto path = static_cast<char*>(mem);

  // Copy the string out from raw memory first
  std::string result(path, max_size);

  // Trim string to first null terminating character
  auto null_pos = result.find('\0');
  if (null_pos != std::string::npos) {
    result.resize(null_pos);
  }

  return std::filesystem::path(std::move(result)).lexically_relative("/");
}

// Awaits the `fuchsia.io.Node/OnOpen` event that is fired when opening with
// `fuchsia.io.OPEN_FLAG_DESCRIBE` flag and returns the status contained in the event.
//
// This is useful for synchronously awaiting the result of an `Open` request.
static zx_status_t AwaitIoOnOpenStatus(const zx::unowned_channel channel) {
  class EventHandler : public fuchsia_io::Node::SyncEventHandler {
   public:
    EventHandler() = default;

    bool call_was_successful() const { return call_was_successful_; }
    zx_status_t status() const { return status_; }

    void OnOpen(fuchsia_io::Node::OnOpenResponse* event) override {
      call_was_successful_ = true;
      status_ = event->s;
    }

    zx_status_t Unknown() override { return ZX_ERR_PROTOCOL_NOT_SUPPORTED; }

    bool call_was_successful_ = false;
    zx_status_t status_ = ZX_OK;
  };

  EventHandler event_handler;
  // TODO(godtamit): check for an epitaph here once `fuchsia.io` (and LLCPP) supports it.
  auto status = event_handler.HandleOneEvent(zx::unowned_channel(channel)).status();
  if (status == ZX_OK) {
    status = event_handler.status();
  }
  if (!event_handler.call_was_successful()) {
    LOG(ERROR, "failed to wait for OnOpen event (status: %d)", status);
  }
  return status;
}

// Calls `fuchsia.io.Directory/Open` on a channel and awaits the result.
static zx_status_t OpenObjectInDirectory(zx::unowned_channel root_channel, uint32_t flags,
                                         uint32_t mode, std::string path,
                                         zx::channel* out_channel_node) {
  ZX_DEBUG_ASSERT(out_channel_node != nullptr);

  // Ensure `OPEN_FLAG_DESCRIBE` is passed
  flags |= fuchsia_io::wire::OPEN_FLAG_DESCRIBE;

  // Create temporary channel ends to make FIDL call
  zx::channel channel_client_end;
  zx::channel channel_server_end;
  zx_status_t status = zx::channel::create(0, &channel_client_end, &channel_server_end);
  if (status != ZX_OK) {
    LOG(ERROR, "failed to create channel pair (status: %d)", status);
    return status;
  }

  auto result = fuchsia_io::Directory::Call::Open(
      std::move(root_channel), flags, mode, fidl::unowned_str(path), std::move(channel_server_end));
  status = result.status();
  if (status != ZX_OK) {
    LOG(ERROR, "could not call fuchsia.io.Directory/Open (status: %d)", status);
    return status;
  }

  status = AwaitIoOnOpenStatus(zx::unowned_channel(channel_client_end));
  if (status != ZX_OK) {
    return status;
  }

  *out_channel_node = std::move(channel_client_end);
  return ZX_OK;
}

// Recursively walks down a multi-part path, opening and outputting the final destination.
//
// Template Parameters:
//  * kOpenFlags: The flags to call `fuchsia.io.Directory/Open` with. This must not contain
//               `OPEN_FLAG_NOT_DIRECTORY`.
// Parameters:
//  * root_channel:     The channel to the directory to start the walk from.
//  * path:             The path relative to `root_channel` to open.
//  * out_node_channel: Where to store the resulting `fuchsia.io.Node` channel opened.
template <uint32_t kOpenFlags>
static zx_status_t RecursivelyWalkPath(zx::unowned_channel& root_channel,
                                       std::filesystem::path path, zx::channel* out_node_channel) {
  static_assert((kOpenFlags & fuchsia_io::wire::OPEN_FLAG_NOT_DIRECTORY) == 0,
                "kOpenFlags must not include fuchsia_io::wire::OPEN_FLAG_NOT_DIRECTORY");
  ZX_DEBUG_ASSERT(root_channel->is_valid());
  ZX_DEBUG_ASSERT(out_node_channel != nullptr);

  zx_status_t status;
  zx::channel result_channel;

  if (path.empty() || path == std::filesystem::path(".")) {
    // If the path is lexicographically equivalent to the (relative) root directory, clone the
    // root channel instead of opening the path. An empty path is considered equivalent to
    // the relative root directory.
    zx::channel server_channel;
    status = zx::channel::create(0, &result_channel, &server_channel);
    if (status != ZX_OK) {
      return status;
    }

    auto result = fuchsia_io::Directory::Call::Clone(zx::unowned_channel(*root_channel),
                                                     fuchsia_io::wire::CLONE_FLAG_SAME_RIGHTS,
                                                     std::move(server_channel));
    status = result.status();
    if (status != ZX_OK) {
      return status;
    }
  } else {
    zx::unowned_channel current_channel(root_channel);
    for (const auto& fragment : path) {
      zx::channel temporary_channel;
      static constexpr uint32_t kOpenMode = fuchsia_io::wire::MODE_TYPE_DIRECTORY;
      status = OpenObjectInDirectory(std::move(current_channel), kOpenFlags, kOpenMode,
                                     fragment.string(), &temporary_channel);
      if (status != ZX_OK) {
        return status;
      }

      result_channel = std::move(temporary_channel);
      current_channel = zx::unowned(result_channel);
    }
  }

  *out_node_channel = std::move(result_channel);
  return ZX_OK;
}

template <typename... Args>
static inline zx_status_t CreateDirectory(Args&&... args) {
  static constexpr uint32_t kCreateFlags =
      fuchsia_io::wire::OPEN_RIGHT_READABLE | fuchsia_io::wire::OPEN_RIGHT_WRITABLE |
      fuchsia_io::wire::OPEN_FLAG_CREATE | fuchsia_io::wire::OPEN_FLAG_DIRECTORY;
  return RecursivelyWalkPath<kCreateFlags>(std::forward<Args>(args)...);
}

template <typename... Args>
static inline zx_status_t OpenDirectory(Args&&... args) {
  static constexpr uint32_t kOpenFlags = fuchsia_io::wire::OPEN_RIGHT_READABLE |
                                         fuchsia_io::wire::OPEN_RIGHT_WRITABLE |
                                         fuchsia_io::wire::OPEN_FLAG_DIRECTORY;
  return RecursivelyWalkPath<kOpenFlags>(std::forward<Args>(args)...);
}

}  // namespace

namespace optee {

zx_status_t OpteeClient::DdkClose(uint32_t flags) {
  // Because each client instance should map to just one client and the client has closed, this
  // instance can safely shut down.
  Shutdown();
  return ZX_OK;
}

void OpteeClient::DdkRelease() { delete this; }

void OpteeClient::DdkSuspend(ddk::SuspendTxn txn) {
  Shutdown();
  txn.Reply(ZX_OK, txn.requested_state());
}

void OpteeClient::DdkUnbind(ddk::UnbindTxn txn) { Shutdown(); }

void OpteeClient::Shutdown() {
  if (controller_ != nullptr) {
    // Try and cleanly close all sessions
    std::vector<uint32_t> session_ids(open_sessions_.size());
    session_ids.assign(open_sessions_.begin(), open_sessions_.end());

    for (uint32_t id : session_ids) {
      // Regardless of CloseSession response, continue closing all other sessions
      __UNUSED zx_status_t status = CloseSession(id);
    }
  }

  // For sanity's sake, mark the controller_ as null to ensure that nothing else gets called.
  controller_ = nullptr;
}

zx_status_t OpteeClient::DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  if (controller_ == nullptr) {
    return ZX_ERR_PEER_CLOSED;
  }
  DdkTransaction transaction(txn);

  fuchsia_tee::Application::Dispatch(this, msg, &transaction);

  return transaction.Status();
}

void OpteeClient::OpenSession2(
    fidl::VectorView<fuchsia_tee::wire::Parameter> parameter_set,
    fuchsia_tee::Application::Interface::OpenSession2Completer::Sync& completer) {
  constexpr uint32_t kInvalidSession = 0;

  fidl::FidlAllocator allocator;
  fuchsia_tee::wire::OpResult result(allocator);

  auto create_result =
      OpenSessionMessage::TryCreate(controller_->driver_pool(), controller_->client_pool(),
                                    application_uuid_, std::move(parameter_set));
  if (!create_result.is_ok()) {
    LOG(ERROR, "failed to create OpenSessionMessage (status: %d)", create_result.error());
    result.set_return_code(allocator, TEEC_ERROR_COMMUNICATION);
    result.set_return_origin(allocator, fuchsia_tee::wire::ReturnOrigin::COMMUNICATION);
    completer.Reply(kInvalidSession, std::move(result));
    return;
  }

  OpenSessionMessage message = create_result.take_value();

  auto [call_code, peak_smc_call_duration] =
      controller_->CallWithMessage(message, fbl::BindMember(this, &OpteeClient::HandleRpc));

  if (peak_smc_call_duration > kSmcCallDurationThreshold) {
    LOG(WARNING,
        "SMC call threshold exceeded. peak_smc_call_duration: %" PRIi64 "ns trusted_app: %s",
        peak_smc_call_duration.to_nsecs(), application_uuid_.ToString().c_str());
  }

  if (call_code != kReturnOk) {
    result.set_return_code(allocator, TEEC_ERROR_COMMUNICATION);
    result.set_return_origin(allocator, fuchsia_tee::wire::ReturnOrigin::COMMUNICATION);
    completer.Reply(kInvalidSession, std::move(result));
    return;
  }

  LOG(TRACE, "OpenSession returned 0x%" PRIx32 " 0x%" PRIx32 " 0x%" PRIx32, call_code,
      message.return_code(), message.return_origin());

  if (ConvertOpteeToZxResult(allocator, message.return_code(), message.return_origin(), &result) !=
      ZX_OK) {
    completer.Reply(kInvalidSession, std::move(result));
    return;
  }

  fidl::VectorView<fuchsia_tee::wire::Parameter> out_parameters;
  if (message.CreateOutputParameterSet(allocator, &out_parameters) != ZX_OK) {
    // Since we failed to parse the output parameters, let's close the session and report error.
    // It is okay that the session id is not in the session list.
    CloseSession(message.session_id());
    result.set_return_code(allocator, TEEC_ERROR_COMMUNICATION);
    result.set_return_origin(allocator, fuchsia_tee::wire::ReturnOrigin::COMMUNICATION);
    completer.Reply(kInvalidSession, std::move(result));
    return;
  }
  result.set_parameter_set(allocator, std::move(out_parameters));
  open_sessions_.insert(message.session_id());

  completer.Reply(message.session_id(), std::move(result));
  return;
}

void OpteeClient::InvokeCommand(
    uint32_t session_id, uint32_t command_id,
    fidl::VectorView<fuchsia_tee::wire::Parameter> parameter_set,
    fuchsia_tee::Application::Interface::InvokeCommandCompleter::Sync& completer) {
  fidl::FidlAllocator allocator;
  fuchsia_tee::wire::OpResult result(allocator);

  if (open_sessions_.find(session_id) == open_sessions_.end()) {
    result.set_return_code(allocator, TEEC_ERROR_BAD_STATE);
    result.set_return_origin(allocator, fuchsia_tee::wire::ReturnOrigin::COMMUNICATION);
    completer.Reply(std::move(result));
    return;
  }

  auto create_result =
      InvokeCommandMessage::TryCreate(controller_->driver_pool(), controller_->client_pool(),
                                      session_id, command_id, std::move(parameter_set));
  if (!create_result.is_ok()) {
    LOG(ERROR, "failed to create InvokeCommandMessage (status: %d)", create_result.error());
    result.set_return_code(allocator, TEEC_ERROR_COMMUNICATION);
    result.set_return_origin(allocator, fuchsia_tee::wire::ReturnOrigin::COMMUNICATION);
    completer.Reply(std::move(result));
    return;
  }

  InvokeCommandMessage message = create_result.take_value();

  auto [call_code, peak_smc_call_duration] =
      controller_->CallWithMessage(message, fbl::BindMember(this, &OpteeClient::HandleRpc));

  if (peak_smc_call_duration > kSmcCallDurationThreshold) {
    LOG(WARNING,
        "SMC call threshold exceeded. peak_smc_call_duration: %" PRIi64
        "ns trusted_app: %s session_id: 0x%" PRIx32 " command_id: 0x%" PRIx32,
        peak_smc_call_duration.to_nsecs(), application_uuid_.ToString().c_str(), session_id,
        command_id);
  }

  if (call_code != kReturnOk) {
    result.set_return_code(allocator, TEEC_ERROR_COMMUNICATION);
    result.set_return_origin(allocator, fuchsia_tee::wire::ReturnOrigin::COMMUNICATION);
    completer.Reply(std::move(result));
    return;
  }

  LOG(TRACE, "InvokeCommand returned 0x%" PRIx32 " 0x%" PRIx32 " 0x%" PRIx32, call_code,
      message.return_code(), message.return_origin());

  if (ConvertOpteeToZxResult(allocator, message.return_code(), message.return_origin(), &result) !=
      ZX_OK) {
    completer.Reply(std::move(result));
    return;
  }

  fidl::VectorView<fuchsia_tee::wire::Parameter> out_parameters;
  if (message.CreateOutputParameterSet(allocator, &out_parameters) != ZX_OK) {
    result.set_return_code(allocator, TEEC_ERROR_COMMUNICATION);
    result.set_return_origin(allocator, fuchsia_tee::wire::ReturnOrigin::COMMUNICATION);
    completer.Reply(std::move(result));
    return;
  }
  result.set_parameter_set(allocator, std::move(out_parameters));

  completer.Reply(std::move(result));
  return;
}

zx_status_t OpteeClient::CloseSession(uint32_t session_id) {
  auto create_result = CloseSessionMessage::TryCreate(controller_->driver_pool(), session_id);
  if (!create_result.is_ok()) {
    LOG(ERROR, "failed to create CloseSessionMessage (status: %d)", create_result.error());
    return create_result.error();
  }

  CloseSessionMessage message = create_result.take_value();

  auto [call_code, peak_smc_call_duration] =
      controller_->CallWithMessage(message, fbl::BindMember(this, &OpteeClient::HandleRpc));

  if (peak_smc_call_duration > kSmcCallDurationThreshold) {
    LOG(WARNING,
        "SMC call threshold exceeded. peak_smc_call_duration: %" PRIi64
        "ns trusted_app: %s session_id: 0x%" PRIx32,
        peak_smc_call_duration.to_nsecs(), application_uuid_.ToString().c_str(), session_id);
  }

  if (call_code == kReturnOk) {
    open_sessions_.erase(session_id);
  }

  LOG(TRACE, "CloseSession returned %" PRIx32 " %" PRIx32 " %" PRIx32, call_code,
      message.return_code(), message.return_origin());
  return ZX_OK;
}

void OpteeClient::CloseSession(
    uint32_t session_id,
    fuchsia_tee::Application::Interface::CloseSessionCompleter::Sync& completer) {
  CloseSession(session_id);
  completer.Reply();
}

template <typename SharedMemoryPoolTraits>
zx_status_t OpteeClient::AllocateSharedMemory(size_t size,
                                              SharedMemoryPool<SharedMemoryPoolTraits>* memory_pool,
                                              zx_paddr_t* out_phys_addr, uint64_t* out_mem_id) {
  ZX_DEBUG_ASSERT(memory_pool != nullptr);
  ZX_DEBUG_ASSERT(out_phys_addr != nullptr);
  ZX_DEBUG_ASSERT(out_mem_id != nullptr);

  // Set these to 0 and overwrite, if necessary, on success path
  *out_phys_addr = 0;
  *out_mem_id = 0;

  if (size == 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  std::unique_ptr<SharedMemory> sh_mem;
  zx_status_t status = memory_pool->Allocate(size, &sh_mem);
  if (status != ZX_OK) {
    return status;
  }

  *out_phys_addr = sh_mem->paddr();

  // Track the new piece of allocated SharedMemory in the list
  allocated_shared_memory_.push_back(std::move(sh_mem));

  // TODO(godtamit): Move away from memory addresses as memory identifiers
  //
  // Make the memory identifier the address of the SharedMemory object
  auto sh_mem_addr = reinterpret_cast<uintptr_t>(&allocated_shared_memory_.back());
  *out_mem_id = static_cast<uint64_t>(sh_mem_addr);

  return status;
}

zx_status_t OpteeClient::FreeSharedMemory(uint64_t mem_id) {
  // Check if client owns memory that matches the memory id
  SharedMemoryList::iterator mem_iter = FindSharedMemory(mem_id);
  if (!mem_iter.IsValid()) {
    return ZX_ERR_NOT_FOUND;
  }

  // Destructor of SharedMemory will automatically free block back into pool
  allocated_shared_memory_.erase(mem_iter);

  return ZX_OK;
}

OpteeClient::SharedMemoryList::iterator OpteeClient::FindSharedMemory(uint64_t mem_id) {
  // TODO(godtamit): Move away from memory addresses as memory identifiers
  auto mem_id_ptr_val = static_cast<uintptr_t>(mem_id);
  return allocated_shared_memory_.find_if([mem_id_ptr_val](auto& item) {
    return mem_id_ptr_val == reinterpret_cast<uintptr_t>(&item);
  });
}

std::optional<SharedMemoryView> OpteeClient::GetMemoryReference(SharedMemoryList::iterator mem_iter,
                                                                zx_paddr_t base_paddr,
                                                                size_t size) {
  std::optional<SharedMemoryView> result;
  if (!mem_iter.IsValid() ||
      !(result = mem_iter->SliceByPaddr(base_paddr, base_paddr + size)).has_value()) {
    LOG(ERROR, "received invalid shared memory region reference");
  }
  return result;
}

zx_status_t OpteeClient::GetRootStorageChannel(zx::unowned_channel* out_root_channel) {
  ZX_DEBUG_ASSERT(out_root_channel != nullptr);

  if (!provider_channel_.is_valid()) {
    return ZX_ERR_UNAVAILABLE;
  }
  if (root_storage_channel_.is_valid()) {
    *out_root_channel = zx::unowned_channel(root_storage_channel_);
    return ZX_OK;
  }

  zx::channel client_channel;
  zx::channel server_channel;
  zx_status_t status = zx::channel::create(0, &client_channel, &server_channel);
  if (status != ZX_OK) {
    return status;
  }

  auto result = ::fuchsia_tee_manager::Provider::Call::RequestPersistentStorage(
      zx::unowned_channel(provider_channel_), std::move(server_channel));
  status = result.status();
  if (status != ZX_OK) {
    return status;
  }

  root_storage_channel_ = std::move(client_channel);
  *out_root_channel = zx::unowned_channel(root_storage_channel_);
  return ZX_OK;
}

zx_status_t OpteeClient::InitRpmbClient(void) {
  if (rpmb_client_.has_value()) {
    return ZX_OK;
  }

  zx::channel client, server;
  zx_status_t status = zx::channel::create(0, &client, &server);
  if (status != ZX_OK) {
    LOG(ERROR, "failed to create channel pair (status: %d)", status);
    return status;
  }

  status = controller_->RpmbConnectServer(std::move(server));
  if (status != ZX_OK) {
    LOG(ERROR, "failed to connect to RPMB server (status: %d)", status);
    return status;
  }

  rpmb_client_ = ::fuchsia_hardware_rpmb::Rpmb::SyncClient(std::move(client));

  return ZX_OK;
}

zx_status_t OpteeClient::GetStorageDirectory(std::filesystem::path path, bool create,
                                             zx::channel* out_storage_channel) {
  ZX_DEBUG_ASSERT(out_storage_channel != nullptr);

  zx::unowned_channel root_channel;
  zx_status_t status = GetRootStorageChannel(&root_channel);
  if (status != ZX_OK) {
    return status;
  }

  zx::channel storage_channel;

  if (create) {
    status = CreateDirectory(root_channel, path, &storage_channel);
  } else {
    status = OpenDirectory(root_channel, path, &storage_channel);
  }
  if (status != ZX_OK) {
    return status;
  }

  *out_storage_channel = std::move(storage_channel);
  return ZX_OK;
}

uint64_t OpteeClient::TrackFileSystemObject(zx::channel io_node_channel) {
  uint64_t object_id = next_file_system_object_id_.fetch_add(1, std::memory_order_relaxed);
  open_file_system_objects_.insert({object_id, std::move(io_node_channel)});

  return object_id;
}

std::optional<zx::unowned_channel> OpteeClient::GetFileSystemObjectChannel(uint64_t identifier) {
  auto iter = open_file_system_objects_.find(identifier);
  if (iter == open_file_system_objects_.end()) {
    return std::nullopt;
  }
  return zx::unowned_channel(iter->second);
}

bool OpteeClient::UntrackFileSystemObject(uint64_t identifier) {
  size_t erase_count = open_file_system_objects_.erase(identifier);
  return erase_count > 0;
}

zx_status_t OpteeClient::HandleRpc(const RpcFunctionArgs& args, RpcFunctionResult* out_result) {
  zx_status_t status;
  uint32_t func_code = GetRpcFunctionCode(args.generic.status);

  switch (func_code) {
    case kRpcFunctionIdAllocateMemory:
      status = HandleRpcAllocateMemory(args.allocate_memory, &out_result->allocate_memory);
      break;
    case kRpcFunctionIdFreeMemory:
      status = HandleRpcFreeMemory(args.free_memory, &out_result->free_memory);
      break;
    case kRpcFunctionIdDeliverIrq:
      // Foreign interrupt detected while in the secure world
      // Zircon handles this so just mark the RPC as handled
      status = ZX_OK;
      break;
    case kRpcFunctionIdExecuteCommand:
      status = HandleRpcCommand(args.execute_command, &out_result->execute_command);
      break;
    default:
      status = ZX_ERR_NOT_SUPPORTED;
      break;
  }

  // Set the function to return from RPC
  out_result->generic.func_id = optee::kReturnFromRpcFuncId;

  return status;
}

zx_status_t OpteeClient::HandleRpcAllocateMemory(const RpcFunctionAllocateMemoryArgs& args,
                                                 RpcFunctionAllocateMemoryResult* out_result) {
  ZX_DEBUG_ASSERT(out_result != nullptr);

  zx_paddr_t paddr;
  uint64_t mem_id;

  zx_status_t status = AllocateSharedMemory(static_cast<size_t>(args.size),
                                            controller_->driver_pool(), &paddr, &mem_id);
  // If allocation failed, AllocateSharedMemory sets paddr and mem_id to 0. Continue with packing
  // those values into the result regardless.

  // Put the physical address of allocated memory in the args
  SplitInto32BitParts(paddr, &out_result->phys_addr_upper32, &out_result->phys_addr_lower32);

  // Pack the memory identifier in the args
  SplitInto32BitParts(mem_id, &out_result->mem_id_upper32, &out_result->mem_id_lower32);

  return status;
}

zx_status_t OpteeClient::HandleRpcFreeMemory(const RpcFunctionFreeMemoryArgs& args,
                                             RpcFunctionFreeMemoryResult* out_result) {
  ZX_DEBUG_ASSERT(out_result != nullptr);

  uint64_t mem_id;
  JoinFrom32BitParts(args.mem_id_upper32, args.mem_id_lower32, &mem_id);

  return FreeSharedMemory(mem_id);
}

zx_status_t OpteeClient::HandleRpcCommand(const RpcFunctionExecuteCommandsArgs& args,
                                          RpcFunctionExecuteCommandsResult* out_result) {
  uint64_t mem_id;
  JoinFrom32BitParts(args.msg_mem_id_upper32, args.msg_mem_id_lower32, &mem_id);

  // Make sure memory where message is stored is valid
  // This dispatcher method only checks that the memory needed for the header is valid. Commands
  // that require more memory than just the header will need to do further memory checks.
  SharedMemoryList::iterator mem_iter = FindSharedMemory(mem_id);
  if (!mem_iter.IsValid() || mem_iter->size() < sizeof(MessageHeader)) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Read message header from shared memory
  SharedMemory& msg_mem = *mem_iter;
  auto message_result = RpcMessage::CreateFromSharedMemory(&msg_mem);
  if (!message_result.is_ok()) {
    return message_result.error();
  }
  RpcMessage message = message_result.take_value();

  // Mark that the return code will originate from driver
  message.set_return_origin(TEEC_ORIGIN_COMMS);

  switch (message.command()) {
    case RpcMessage::Command::kLoadTa: {
      auto load_ta_result = LoadTaRpcMessage::CreateFromRpcMessage(std::move(message));
      if (!load_ta_result.is_ok()) {
        return load_ta_result.error();
      }
      return HandleRpcCommandLoadTa(&load_ta_result.value());
    }
    case RpcMessage::Command::kAccessFileSystem: {
      auto fs_result = FileSystemRpcMessage::CreateFromRpcMessage(std::move(message));
      if (!fs_result.is_ok()) {
        return fs_result.error();
      }
      return HandleRpcCommandFileSystem(fs_result.take_value());
    }
    case RpcMessage::Command::kGetTime: {
      auto get_time_result = GetTimeRpcMessage::CreateFromRpcMessage(std::move(message));
      if (!get_time_result.is_ok()) {
        return get_time_result.error();
      }
      return HandleRpcCommandGetTime(&get_time_result.value());
    }
    case RpcMessage::Command::kWaitQueue:
      LOG(DEBUG, "RPC command wait queue recognized but not implemented");
      return ZX_ERR_NOT_SUPPORTED;
    case RpcMessage::Command::kSuspend:
      LOG(DEBUG, "RPC command to suspend recognized but not implemented");
      return ZX_ERR_NOT_SUPPORTED;
    case RpcMessage::Command::kAllocateMemory: {
      auto alloc_mem_result = AllocateMemoryRpcMessage::CreateFromRpcMessage(std::move(message));
      if (!alloc_mem_result.is_ok()) {
        return alloc_mem_result.error();
      }
      return HandleRpcCommandAllocateMemory(&alloc_mem_result.value());
    }
    case RpcMessage::Command::kFreeMemory: {
      auto free_mem_result = FreeMemoryRpcMessage::CreateFromRpcMessage(std::move(message));
      if (!free_mem_result.is_ok()) {
        return free_mem_result.error();
      }
      return HandleRpcCommandFreeMemory(&free_mem_result.value());
    }
    case RpcMessage::Command::kPerformSocketIo:
      LOG(DEBUG, "RPC command to perform socket IO recognized but not implemented");
      message.set_return_code(TEEC_ERROR_NOT_SUPPORTED);
      return ZX_OK;
    case RpcMessage::Command::kAccessReplayProtectedMemoryBlock: {
      LOG(DEBUG, "RPC command to access RPMB");
      auto rpmb_access_result = RpmbRpcMessage::CreateFromRpcMessage(std::move(message));
      if (!rpmb_access_result.is_ok()) {
        return rpmb_access_result.error();
      }
      return HandleRpcCommandAccessRpmb(&rpmb_access_result.value());
    }
    case RpcMessage::Command::kAccessSqlFileSystem:
    case RpcMessage::Command::kLoadGprof:
      LOG(DEBUG, "optee: received unsupported RPC command");
      message.set_return_code(TEEC_ERROR_NOT_SUPPORTED);
      return ZX_OK;
    default:
      LOG(ERROR, "unrecognized command passed to RPC 0x%" PRIu32, message.command());
      message.set_return_code(TEEC_ERROR_NOT_SUPPORTED);
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t OpteeClient::HandleRpcCommandLoadTa(LoadTaRpcMessage* message) {
  ZX_DEBUG_ASSERT(message != nullptr);

  // Try to find the SharedMemory based on the memory id
  std::optional<SharedMemoryView> out_ta_mem;  // Where to write the TA in memory

  if (message->memory_reference_id() != 0) {
    out_ta_mem =
        GetMemoryReference(FindSharedMemory(message->memory_reference_id()),
                           message->memory_reference_paddr(), message->memory_reference_size());
    if (!out_ta_mem.has_value()) {
      message->set_return_code(TEEC_ERROR_BAD_PARAMETERS);
      return ZX_ERR_INVALID_ARGS;
    }
  } else {
    // TEE is just querying size of TA, so it sent a memory identifier of 0
    ZX_DEBUG_ASSERT(message->memory_reference_size() == 0);
  }

  auto ta_path = BuildTaPath(message->ta_uuid());

  // Load the trusted app into a VMO
  size_t ta_size;
  zx::vmo ta_vmo;
  zx_status_t status = load_firmware(controller_->GetDevice(), ta_path.data(),
                                     ta_vmo.reset_and_get_address(), &ta_size);

  if (status != ZX_OK) {
    if (status == ZX_ERR_NOT_FOUND) {
      LOG(DEBUG, "could not find trusted app %s!", ta_path.data());
      message->set_return_code(TEEC_ERROR_ITEM_NOT_FOUND);
    } else {
      LOG(DEBUG, "error loading trusted app %s!", ta_path.data());
      message->set_return_code(TEEC_ERROR_GENERIC);
    }

    return status;
  } else if (ta_size == 0) {
    LOG(ERROR, "loaded trusted app %s with unexpected size!", ta_path.data());
    message->set_return_code(TEEC_ERROR_GENERIC);
    return status;
  }

  message->set_output_ta_size(static_cast<uint64_t>(ta_size));

  if (!out_ta_mem.has_value()) {
    // TEE is querying the size of the TA
    message->set_return_code(TEEC_SUCCESS);
    return ZX_OK;
  } else if (ta_size > out_ta_mem->size()) {
    // TEE provided too small of a memory region to write TA into
    message->set_return_code(TEEC_ERROR_SHORT_BUFFER);
    return ZX_OK;
  }

  // TODO(godtamit): in the future, we may want to register the memory as shared and use its VMO,
  // so we don't have to do a copy of the TA
  status = ta_vmo.read(reinterpret_cast<void*>(out_ta_mem->vaddr()), 0, ta_size);
  if (status != ZX_OK) {
    LOG(ERROR, "failed to copy trusted app from VMO to shared memory!");
    message->set_return_code(TEEC_ERROR_GENERIC);
    return status;
  }

  if (ta_size < out_ta_mem->size()) {
    // Clear out the rest of the memory after the TA
    void* ta_end = reinterpret_cast<void*>(out_ta_mem->vaddr() + ta_size);
    ::memset(ta_end, 0, out_ta_mem->size() - ta_size);
  }

  message->set_return_code(TEEC_SUCCESS);
  return ZX_OK;
}

zx_status_t OpteeClient::HandleRpcCommandAccessRpmb(RpmbRpcMessage* message) {
  ZX_DEBUG_ASSERT(message != nullptr);
  RpmbReq* req;
  zx_status_t status;

  // Try to find the SharedMemory based on the memory id
  std::optional<SharedMemoryView> tx_frame_mem;
  std::optional<SharedMemoryView> rx_frame_mem;

  if (message->tx_memory_reference_id() != 0) {
    tx_frame_mem = GetMemoryReference(FindSharedMemory(message->tx_memory_reference_id()),
                                      message->tx_memory_reference_paddr(),
                                      message->tx_memory_reference_size());
    if (!tx_frame_mem.has_value()) {
      message->set_return_code(TEEC_ERROR_BAD_PARAMETERS);
      return ZX_ERR_INVALID_ARGS;
    }
  } else {
    ZX_DEBUG_ASSERT(message->tx_memory_reference_size() == 0);
  }

  if (message->rx_memory_reference_id() != 0) {
    rx_frame_mem = GetMemoryReference(FindSharedMemory(message->rx_memory_reference_id()),
                                      message->rx_memory_reference_paddr(),
                                      message->rx_memory_reference_size());
    if (!rx_frame_mem.has_value()) {
      message->set_return_code(TEEC_ERROR_BAD_PARAMETERS);
      return ZX_ERR_INVALID_ARGS;
    }
  } else {
    ZX_DEBUG_ASSERT(message->rx_memory_reference_size() == 0);
  }

  if (!tx_frame_mem || !rx_frame_mem) {
    return ZX_ERR_INVALID_ARGS;
  }

  req = reinterpret_cast<RpmbReq*>(tx_frame_mem->vaddr());

  switch (req->cmd) {
    case RpmbReq::kCmdGetDevInfo:
      status = RpmbGetDevInfo(tx_frame_mem, rx_frame_mem);
      break;
    case RpmbReq::kCmdDataRequest: {
      std::optional<SharedMemoryView> new_tx_frame_mem = tx_frame_mem->SliceByVaddr(
          tx_frame_mem->vaddr() + sizeof(RpmbReq), tx_frame_mem->vaddr() + tx_frame_mem->size());
      status = RpmbRouteFrames(std::move(new_tx_frame_mem), std::move(rx_frame_mem));
      break;
    }
    default:
      LOG(ERROR, "Unknown RPMB request command: %d", req->cmd);
      status = ZX_ERR_INVALID_ARGS;
  }

  int ret;
  switch (status) {
    case ZX_OK:
      ret = TEEC_SUCCESS;
      break;
    case ZX_ERR_INVALID_ARGS:
      ret = TEEC_ERROR_BAD_PARAMETERS;
      break;
    case ZX_ERR_UNAVAILABLE:
      ret = TEEC_ERROR_ITEM_NOT_FOUND;
      break;
    case ZX_ERR_NOT_SUPPORTED:
      ret = TEEC_ERROR_NOT_SUPPORTED;
      break;
    case ZX_ERR_PEER_CLOSED:
      ret = TEEC_ERROR_COMMUNICATION;
      break;
    default:
      ret = TEEC_ERROR_GENERIC;
  }

  message->set_return_code(ret);
  return status;
}

zx_status_t OpteeClient::RpmbGetDevInfo(std::optional<SharedMemoryView> tx_frames,
                                        std::optional<SharedMemoryView> rx_frames) {
  zx::unowned_channel rpmb_channel;

  ZX_DEBUG_ASSERT(tx_frames.has_value());
  ZX_DEBUG_ASSERT(rx_frames.has_value());

  if ((tx_frames->size() != sizeof(RpmbReq)) || (rx_frames->size() != sizeof(RpmbDevInfo))) {
    LOG(ERROR, "Wrong TX or RX frames size");
    return ZX_ERR_INVALID_ARGS;
  }

  zx_status_t status = InitRpmbClient();
  if (status != ZX_OK) {
    return status;
  }

  auto result = rpmb_client_->GetDeviceInfo();
  status = result.status();
  if (status != ZX_OK) {
    LOG(ERROR, "Failed to get RPMB Device Info (status: %d)", status);
    return status;
  }

  RpmbDevInfo* info = reinterpret_cast<RpmbDevInfo*>(rx_frames->vaddr());

  if (result->info.is_emmc_info()) {
    memcpy(info->cid, result->info.emmc_info().cid.data(), RpmbDevInfo::kRpmbCidSize);
    info->rpmb_size = result->info.emmc_info().rpmb_size;
    info->rel_write_sector_count = result->info.emmc_info().reliable_write_sector_count;
    info->ret_code = RpmbDevInfo::kRpmbCmdRetOK;
  } else {
    info->ret_code = RpmbDevInfo::kRpmbCmdRetError;
  }

  return ZX_OK;
}

zx_status_t OpteeClient::RpmbRouteFrames(std::optional<SharedMemoryView> tx_frames,
                                         std::optional<SharedMemoryView> rx_frames) {
  ZX_DEBUG_ASSERT(tx_frames.has_value());
  ZX_DEBUG_ASSERT(rx_frames.has_value());

  using ::fuchsia_hardware_rpmb::wire::FRAME_SIZE;

  zx_status_t status;
  RpmbFrame* frame = reinterpret_cast<RpmbFrame*>(tx_frames->vaddr());

  if ((tx_frames->size() % FRAME_SIZE) || (rx_frames->size() % FRAME_SIZE)) {
    return ZX_ERR_INVALID_ARGS;
  }

  uint64_t tx_frame_cnt = tx_frames->size() / FRAME_SIZE;
  uint64_t rx_frame_cnt = rx_frames->size() / FRAME_SIZE;

  switch (betoh16(frame->request)) {
    case RpmbFrame::kRpmbRequestKey:
      LOG(DEBUG, "Receive RPMB::kRpmbRequestKey frame\n");
      if ((tx_frame_cnt != 1) || (rx_frame_cnt != 1)) {
        return ZX_ERR_INVALID_ARGS;
      }
      status = RpmbWriteRequest(std::move(tx_frames), std::move(rx_frames));
      break;
    case RpmbFrame::kRpmbRequestWCounter:
      LOG(DEBUG, "Receive RPMB::kRpmbRequestWCounter frame\n");
      if (tx_frame_cnt != 1 || (rx_frame_cnt != 1)) {
        return ZX_ERR_INVALID_ARGS;
      }
      status = RpmbReadRequest(std::move(tx_frames), std::move(rx_frames));
      break;
    case RpmbFrame::kRpmbRequestWriteData:
      LOG(DEBUG, "Receive RPMB::kRpmbRequestWriteData frame\n");
      if ((tx_frame_cnt != 1) || (rx_frame_cnt != 1)) {
        return ZX_ERR_INVALID_ARGS;
      }
      status = RpmbWriteRequest(std::move(tx_frames), std::move(rx_frames));
      break;
    case RpmbFrame::kRpmbRequestReadData:
      LOG(DEBUG, "Receive RPMB::kRpmbRequestReadData frame\n");
      if ((tx_frame_cnt != 1) || !rx_frame_cnt) {
        return ZX_ERR_INVALID_ARGS;
      }
      status = RpmbReadRequest(std::move(tx_frames), std::move(rx_frames));
      break;
    default:
      LOG(ERROR, "Unknown RPMB frame: %d", betoh16(frame->request));
      status = ZX_ERR_INVALID_ARGS;
  }

  return status;
}

zx_status_t OpteeClient::RpmbReadRequest(std::optional<SharedMemoryView> tx_frames,
                                         std::optional<SharedMemoryView> rx_frames) {
  return RpmbSendRequest(tx_frames, rx_frames);
}

zx_status_t OpteeClient::RpmbWriteRequest(std::optional<SharedMemoryView> tx_frames,
                                          std::optional<SharedMemoryView> rx_frames) {
  ZX_DEBUG_ASSERT(tx_frames.has_value());
  ZX_DEBUG_ASSERT(rx_frames.has_value());

  zx_status_t status;
  std::optional<SharedMemoryView> empty = {};
  status = RpmbSendRequest(tx_frames, empty);
  if (status != ZX_OK) {
    LOG(ERROR, "Failed to send RPMB write request (status: %d)", status);
    return status;
  }

  RpmbFrame* frame = reinterpret_cast<RpmbFrame*>(rx_frames->vaddr());
  memset(reinterpret_cast<void*>(rx_frames->vaddr()), 0, rx_frames->size());
  frame->request = htobe16(RpmbFrame::kRpmbRequestStatus);
  status = RpmbSendRequest(rx_frames, rx_frames);
  if (status != ZX_OK) {
    LOG(ERROR, "Failed to send RPMB response request (status: %d)", status);
    return status;
  }

  return ZX_OK;
}

zx_status_t OpteeClient::RpmbSendRequest(std::optional<SharedMemoryView>& req,
                                         std::optional<SharedMemoryView>& resp) {
  ZX_DEBUG_ASSERT(req.has_value());
  // One VMO contains both TX and RX frames:
  // Offset: 0           TX size        TX size aligned            RX size
  //                                      by PAGE SIZE
  //         |   TX FRAMES  |     padding      |        RX FRAMES     |
  zx::vmo rpmb_vmo;
  uint64_t size = fbl::round_up(req->size(), ZX_PAGE_SIZE);
  bool has_rx_frames = resp && resp->size();
  uint64_t rx_offset = size;

  zx_status_t status = InitRpmbClient();
  if (status != ZX_OK) {
    return status;
  }

  if (has_rx_frames) {
    size += fbl::round_up(resp->size(), ZX_PAGE_SIZE);
  }

  ::fuchsia_hardware_rpmb::wire::Request rpmb_request = {};

  status = zx::vmo::create(size, 0, &rpmb_vmo);
  if (status != ZX_OK) {
    LOG(ERROR, "Failed to create VMO for RPMB frames (status: %d)", status);
    return status;
  }

  status = rpmb_vmo.write(reinterpret_cast<void*>(req->vaddr()), 0, req->size());
  if (status != ZX_OK) {
    LOG(ERROR, "Failed to write request into RPMP TX VMO (status: %d)", status);
    return status;
  }
  rpmb_request.tx_frames.offset = 0;
  rpmb_request.tx_frames.size = req->size();
  status = rpmb_vmo.duplicate(ZX_RIGHTS_BASIC | ZX_RIGHT_READ | ZX_RIGHT_MAP,
                              &rpmb_request.tx_frames.vmo);
  if (status != ZX_OK) {
    LOG(ERROR, "Failed to duplicate the RPMB TX VMO to RPMB Request (status: %d)", status);
    return status;
  }

  ::fuchsia_mem::wire::Range rx_frames_range = {};

  if (has_rx_frames) {
    status = rpmb_vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &rx_frames_range.vmo);
    if (status != ZX_OK) {
      LOG(ERROR, "Failed to duplicate the RPMB RX VMO to RPMB Request (status: %d)", status);
      return status;
    }

    rx_frames_range.offset = rx_offset;
    rx_frames_range.size = resp->size();
    rpmb_request.rx_frames = fidl::unowned_ptr_t<::fuchsia_mem::wire::Range>(&rx_frames_range);
  }

  auto res = rpmb_client_->Request(std::move(rpmb_request));
  status = res.status();
  if ((status == ZX_OK) && (res->result.is_err())) {
    status = res->result.err();
  }

  if (status != ZX_OK) {
    LOG(ERROR, "Failed to call RPMB exec Request (status: %d)", status);
    return status;
  }

  if (has_rx_frames) {
    status = rpmb_vmo.read(reinterpret_cast<void*>(resp->vaddr()), rx_offset, resp->size());
  }

  return status;
}

zx_status_t OpteeClient::HandleRpcCommandGetTime(GetTimeRpcMessage* message) {
  // Mark that the return code will originate from driver
  message->set_return_origin(TEEC_ORIGIN_COMMS);

  std::timespec ts;
  if (!std::timespec_get(&ts, TIME_UTC)) {
    message->set_return_code(TEEC_ERROR_GENERIC);
    return ZX_ERR_UNAVAILABLE;
  }

  message->set_output_seconds(ts.tv_sec);
  message->set_output_nanoseconds(ts.tv_nsec);
  message->set_return_code(TEEC_SUCCESS);

  return ZX_OK;
}

zx_status_t OpteeClient::HandleRpcCommandAllocateMemory(AllocateMemoryRpcMessage* message) {
  ZX_DEBUG_ASSERT(message != nullptr);

  if (message->memory_type() == SharedMemoryType::kGlobal) {
    LOG(DEBUG, "implementation currently does not support global shared memory!");
    message->set_return_code(TEEC_ERROR_NOT_SUPPORTED);
    return ZX_ERR_NOT_SUPPORTED;
  }

  size_t size = message->memory_size();
  zx_paddr_t paddr;
  uint64_t mem_id;
  zx_status_t status = AllocateSharedMemory(size, controller_->client_pool(), &paddr, &mem_id);
  if (status != ZX_OK) {
    if (status == ZX_ERR_NO_MEMORY) {
      message->set_return_code(TEEC_ERROR_OUT_OF_MEMORY);
    } else {
      message->set_return_code(TEEC_ERROR_GENERIC);
    }

    return status;
  }

  message->set_output_memory_size(size);
  message->set_output_buffer(paddr);
  message->set_output_memory_identifier(mem_id);

  message->set_return_code(TEEC_SUCCESS);

  return status;
}

zx_status_t OpteeClient::HandleRpcCommandFreeMemory(FreeMemoryRpcMessage* message) {
  ZX_DEBUG_ASSERT(message != nullptr);

  if (message->memory_type() == SharedMemoryType::kGlobal) {
    LOG(DEBUG, "implementation currently does not support global shared memory!");
    message->set_return_code(TEEC_ERROR_NOT_SUPPORTED);
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t status = FreeSharedMemory(message->memory_identifier());
  if (status != ZX_OK) {
    if (status == ZX_ERR_NOT_FOUND) {
      message->set_return_code(TEEC_ERROR_ITEM_NOT_FOUND);
    } else {
      message->set_return_code(TEEC_ERROR_GENERIC);
    }

    return status;
  }

  message->set_return_code(TEEC_SUCCESS);
  return status;
}

zx_status_t OpteeClient::HandleRpcCommandFileSystem(FileSystemRpcMessage&& message) {
  // Mark that the return code will originate from driver
  message.set_return_origin(TEEC_ORIGIN_COMMS);

  if (!provider_channel_.is_valid()) {
    LOG(ERROR, "Filesystem RPC received with !provider_channel_.is_valid()");
    // Client did not connect with a Provider so none of these RPCs can be serviced
    message.set_return_code(TEEC_ERROR_BAD_STATE);
    return ZX_ERR_UNAVAILABLE;
  }

  switch (message.file_system_command()) {
    case FileSystemRpcMessage::FileSystemCommand::kOpenFile: {
      auto result = OpenFileFileSystemRpcMessage::CreateFromFsRpcMessage(std::move(message));
      if (!result.is_ok()) {
        return result.error();
      }
      return HandleRpcCommandFileSystemOpenFile(&result.value());
    }
    case FileSystemRpcMessage::FileSystemCommand::kCreateFile: {
      auto result = CreateFileFileSystemRpcMessage::CreateFromFsRpcMessage(std::move(message));
      if (!result.is_ok()) {
        return result.error();
      }
      return HandleRpcCommandFileSystemCreateFile(&result.value());
    }
    case FileSystemRpcMessage::FileSystemCommand::kCloseFile: {
      auto result = CloseFileFileSystemRpcMessage::CreateFromFsRpcMessage(std::move(message));
      if (!result.is_ok()) {
        return result.error();
      }
      return HandleRpcCommandFileSystemCloseFile(&result.value());
    }
    case FileSystemRpcMessage::FileSystemCommand::kReadFile: {
      auto result = ReadFileFileSystemRpcMessage::CreateFromFsRpcMessage(std::move(message));
      if (!result.is_ok()) {
        return result.error();
      }
      return HandleRpcCommandFileSystemReadFile(&result.value());
    }
    case FileSystemRpcMessage::FileSystemCommand::kWriteFile: {
      auto result = WriteFileFileSystemRpcMessage::CreateFromFsRpcMessage(std::move(message));
      if (!result.is_ok()) {
        return result.error();
      }
      return HandleRpcCommandFileSystemWriteFile(&result.value());
    }
    case FileSystemRpcMessage::FileSystemCommand::kTruncateFile: {
      auto result = TruncateFileFileSystemRpcMessage::CreateFromFsRpcMessage(std::move(message));
      if (!result.is_ok()) {
        return result.error();
      }
      return HandleRpcCommandFileSystemTruncateFile(&result.value());
    }
    case FileSystemRpcMessage::FileSystemCommand::kRemoveFile: {
      auto result = RemoveFileFileSystemRpcMessage::CreateFromFsRpcMessage(std::move(message));
      if (!result.is_ok()) {
        return result.error();
      }
      return HandleRpcCommandFileSystemRemoveFile(&result.value());
    }
    case FileSystemRpcMessage::FileSystemCommand::kRenameFile: {
      auto result = RenameFileFileSystemRpcMessage::CreateFromFsRpcMessage(std::move(message));
      if (!result.is_ok()) {
        return result.error();
      }
      return HandleRpcCommandFileSystemRenameFile(&result.value());
    }
    case FileSystemRpcMessage::FileSystemCommand::kOpenDirectory:
      LOG(DEBUG, "RPC command to open directory recognized but not implemented");
      break;
    case FileSystemRpcMessage::FileSystemCommand::kCloseDirectory:
      LOG(DEBUG, "RPC command to close directory recognized but not implemented");
      break;
    case FileSystemRpcMessage::FileSystemCommand::kGetNextFileInDirectory:
      LOG(DEBUG, "RPC command to get next file in directory recognized but not implemented");
      break;
  }

  message.set_return_code(TEEC_ERROR_NOT_SUPPORTED);
  return ZX_OK;
}

zx_status_t OpteeClient::HandleRpcCommandFileSystemOpenFile(OpenFileFileSystemRpcMessage* message) {
  ZX_DEBUG_ASSERT(message != nullptr);
  ZX_DEBUG_ASSERT(provider_channel_.is_valid());

  LOG(TRACE, "received RPC to open file");

  SharedMemoryList::iterator mem_iter = FindSharedMemory(message->path_memory_identifier());
  std::optional<SharedMemoryView> path_mem =
      GetMemoryReference(mem_iter, message->path_memory_paddr(), message->path_memory_size());
  if (!path_mem.has_value()) {
    message->set_return_code(TEEC_ERROR_BAD_PARAMETERS);
    return ZX_ERR_INVALID_ARGS;
  }

  std::filesystem::path path =
      GetPathFromRawMemory(reinterpret_cast<void*>(path_mem->vaddr()), message->path_memory_size());

  zx::channel storage_channel;
  constexpr bool kNoCreate = false;
  zx_status_t status = GetStorageDirectory(path.parent_path(), kNoCreate, &storage_channel);
  if (status == ZX_ERR_NOT_FOUND) {
    LOG(DEBUG, "parent path not found (status: %d)", status);
    message->set_return_code(TEEC_ERROR_ITEM_NOT_FOUND);
    return status;
  } else if (status != ZX_OK) {
    LOG(DEBUG, "unable to get parent directory (status: %d)", status);
    message->set_return_code(TEEC_ERROR_BAD_STATE);
    return status;
  }

  zx::channel file_channel;
  static constexpr uint32_t kOpenFlags =
      fuchsia_io::wire::OPEN_RIGHT_READABLE | fuchsia_io::wire::OPEN_RIGHT_WRITABLE |
      fuchsia_io::wire::OPEN_FLAG_NOT_DIRECTORY | fuchsia_io::wire::OPEN_FLAG_DESCRIBE;
  static constexpr uint32_t kOpenMode = fuchsia_io::wire::MODE_TYPE_FILE;
  status = OpenObjectInDirectory(zx::unowned_channel(storage_channel), kOpenFlags, kOpenMode,
                                 path.filename().string(), &file_channel);
  if (status == ZX_ERR_NOT_FOUND) {
    LOG(DEBUG, "file not found (status: %d)", status);
    message->set_return_code(TEEC_ERROR_ITEM_NOT_FOUND);
    return status;
  } else if (status != ZX_OK) {
    LOG(DEBUG, "unable to open file (status: %d)", status);
    message->set_return_code(TEEC_ERROR_GENERIC);
    return status;
  }

  uint64_t object_id = TrackFileSystemObject(std::move(file_channel));

  message->set_output_file_system_object_identifier(object_id);
  message->set_return_code(TEEC_SUCCESS);
  return ZX_OK;
}

zx_status_t OpteeClient::HandleRpcCommandFileSystemCreateFile(
    CreateFileFileSystemRpcMessage* message) {
  ZX_DEBUG_ASSERT(message != nullptr);

  LOG(TRACE, "received RPC to create file");

  std::optional<SharedMemoryView> path_mem =
      GetMemoryReference(FindSharedMemory(message->path_memory_identifier()),
                         message->path_memory_paddr(), message->path_memory_size());
  if (!path_mem.has_value()) {
    message->set_return_code(TEEC_ERROR_BAD_PARAMETERS);
    return ZX_ERR_INVALID_ARGS;
  }

  std::filesystem::path path =
      GetPathFromRawMemory(reinterpret_cast<void*>(path_mem->vaddr()), message->path_memory_size());

  zx::channel storage_channel;
  constexpr bool kCreate = true;
  zx_status_t status = GetStorageDirectory(path.parent_path(), kCreate, &storage_channel);
  if (status != ZX_OK) {
    message->set_return_code(TEEC_ERROR_BAD_STATE);
    return status;
  }

  zx::channel file_channel;
  static constexpr uint32_t kCreateFlags =
      fuchsia_io::wire::OPEN_RIGHT_READABLE | fuchsia_io::wire::OPEN_RIGHT_WRITABLE |
      fuchsia_io::wire::OPEN_FLAG_CREATE | fuchsia_io::wire::OPEN_FLAG_DESCRIBE;
  static constexpr uint32_t kCreateMode = fuchsia_io::wire::MODE_TYPE_FILE;
  status = OpenObjectInDirectory(zx::unowned_channel(storage_channel), kCreateFlags, kCreateMode,
                                 path.filename().string(), &file_channel);
  if (status != ZX_OK) {
    LOG(DEBUG, "unable to create file (status: %d)", status);
    message->set_return_code(status == ZX_ERR_ALREADY_EXISTS ? TEEC_ERROR_ACCESS_CONFLICT
                                                             : TEEC_ERROR_GENERIC);
    return status;
  }

  uint64_t object_id = TrackFileSystemObject(std::move(file_channel));

  message->set_output_file_system_object_identifier(object_id);
  message->set_return_code(TEEC_SUCCESS);
  return ZX_OK;
}

zx_status_t OpteeClient::HandleRpcCommandFileSystemCloseFile(
    CloseFileFileSystemRpcMessage* message) {
  ZX_DEBUG_ASSERT(message != nullptr);

  LOG(TRACE, "received RPC to close file");

  if (!UntrackFileSystemObject(message->file_system_object_identifier())) {
    LOG(ERROR, "could not find the requested file to close");
    message->set_return_code(TEEC_ERROR_ITEM_NOT_FOUND);
    return ZX_ERR_NOT_FOUND;
  }

  message->set_return_code(TEEC_SUCCESS);
  return ZX_OK;
}

zx_status_t OpteeClient::HandleRpcCommandFileSystemReadFile(ReadFileFileSystemRpcMessage* message) {
  ZX_DEBUG_ASSERT(message != nullptr);

  LOG(TRACE, "received RPC to read from file");

  auto maybe_file_channel = GetFileSystemObjectChannel(message->file_system_object_identifier());
  if (!maybe_file_channel.has_value()) {
    message->set_return_code(TEEC_ERROR_ITEM_NOT_FOUND);
    return ZX_ERR_NOT_FOUND;
  }

  zx::unowned_channel file_channel(std::move(*maybe_file_channel));

  std::optional<SharedMemoryView> buffer_mem = GetMemoryReference(
      FindSharedMemory(message->file_contents_memory_identifier()),
      message->file_contents_memory_paddr(), message->file_contents_memory_size());
  if (!buffer_mem.has_value()) {
    message->set_return_code(TEEC_ERROR_BAD_PARAMETERS);
    return ZX_ERR_INVALID_ARGS;
  }

  zx_status_t status = ZX_OK;
  zx_status_t io_status = ZX_OK;
  uint8_t* buffer = reinterpret_cast<uint8_t*>(buffer_mem->vaddr());
  uint64_t offset = message->file_offset();
  size_t bytes_left = buffer_mem->size();
  size_t bytes_read = 0;
  fidl::Buffer<fuchsia_io::File::ReadAtRequest> request_buffer;
  fidl::Buffer<fuchsia_io::File::ReadAtResponse> response_buffer;
  while (bytes_left > 0) {
    uint64_t read_chunk_request = std::min(bytes_left, fuchsia_io::wire::MAX_BUF);
    uint64_t read_chunk_actual = 0;

    auto result =
        fuchsia_io::File::Call::ReadAt(zx::unowned_channel(file_channel), request_buffer.view(),
                                       read_chunk_request, offset, response_buffer.view());
    io_status = result->s;
    if (status != ZX_OK || io_status != ZX_OK) {
      LOG(ERROR, "failed to read from file (FIDL status: %d, IO status: %d)", status, io_status);
      message->set_return_code(TEEC_ERROR_GENERIC);
      return status;
    }

    const auto& data = result->data;
    read_chunk_actual = data.count();
    memcpy(buffer, data.begin(), read_chunk_actual);
    buffer += read_chunk_actual;
    offset += read_chunk_actual;
    bytes_left -= read_chunk_actual;
    bytes_read += read_chunk_actual;

    if (read_chunk_actual == 0) {
      break;
    }
  }

  message->set_output_file_contents_size(bytes_read);
  message->set_return_code(TEEC_SUCCESS);
  return ZX_OK;
}

zx_status_t OpteeClient::HandleRpcCommandFileSystemWriteFile(
    WriteFileFileSystemRpcMessage* message) {
  ZX_DEBUG_ASSERT(message != nullptr);

  LOG(TRACE, "received RPC to write file");

  auto maybe_file_channel = GetFileSystemObjectChannel(message->file_system_object_identifier());
  if (!maybe_file_channel.has_value()) {
    message->set_return_code(TEEC_ERROR_ITEM_NOT_FOUND);
    return ZX_ERR_NOT_FOUND;
  }

  zx::unowned_channel file_channel(std::move(*maybe_file_channel));

  std::optional<SharedMemoryView> buffer_mem = GetMemoryReference(
      FindSharedMemory(message->file_contents_memory_identifier()),
      message->file_contents_memory_paddr(), message->file_contents_memory_size());
  if (!buffer_mem.has_value()) {
    message->set_return_code(TEEC_ERROR_BAD_PARAMETERS);
    return ZX_ERR_INVALID_ARGS;
  }

  zx_status_t status = ZX_OK;
  zx_status_t io_status = ZX_OK;
  uint8_t* buffer = reinterpret_cast<uint8_t*>(buffer_mem->vaddr());
  uint64_t offset = message->file_offset();
  size_t bytes_left = message->file_contents_memory_size();
  while (bytes_left > 0) {
    uint64_t write_chunk_request = std::min(bytes_left, fuchsia_io::wire::MAX_BUF);

    auto result = fuchsia_io::File::Call::WriteAt(
        zx::unowned_channel(file_channel),
        fidl::VectorView(fidl::unowned_ptr(buffer), write_chunk_request), offset);
    status = result.status();
    io_status = result->s;
    buffer += result->actual;
    offset += result->actual;
    bytes_left -= result->actual;

    if (status != ZX_OK || io_status != ZX_OK) {
      LOG(ERROR, "failed to write to file (FIDL status: %d, IO status: %d)", status, io_status);
      message->set_return_code(TEEC_ERROR_GENERIC);
      return status;
    }
  }

  message->set_return_code(TEEC_SUCCESS);
  return ZX_OK;
}

zx_status_t OpteeClient::HandleRpcCommandFileSystemTruncateFile(
    TruncateFileFileSystemRpcMessage* message) {
  ZX_DEBUG_ASSERT(message != nullptr);

  LOG(TRACE, "received RPC to truncate file");

  auto maybe_file_channel = GetFileSystemObjectChannel(message->file_system_object_identifier());
  if (!maybe_file_channel.has_value()) {
    message->set_return_code(TEEC_ERROR_ITEM_NOT_FOUND);
    return ZX_ERR_NOT_FOUND;
  }

  zx::unowned_channel file_channel(std::move(*maybe_file_channel));

  auto result = fuchsia_io::File::Call::Truncate(zx::unowned_channel(file_channel),
                                                 message->target_file_size());
  zx_status_t status = result.status();
  zx_status_t io_status = result->s;
  if (status != ZX_OK || io_status != ZX_OK) {
    LOG(ERROR, "failed to truncate file (FIDL status: %d, IO status: %d)", status, io_status);
    message->set_return_code(TEEC_ERROR_GENERIC);
    return status;
  }

  message->set_return_code(TEEC_SUCCESS);
  return ZX_OK;
}

zx_status_t OpteeClient::HandleRpcCommandFileSystemRemoveFile(
    RemoveFileFileSystemRpcMessage* message) {
  ZX_DEBUG_ASSERT(message != nullptr);

  LOG(TRACE, "received RPC to remove file");

  std::optional<SharedMemoryView> path_mem =
      GetMemoryReference(FindSharedMemory(message->path_memory_identifier()),
                         message->path_memory_paddr(), message->path_memory_size());
  if (!path_mem.has_value()) {
    message->set_return_code(TEEC_ERROR_BAD_PARAMETERS);
    return ZX_ERR_INVALID_ARGS;
  }

  std::filesystem::path path =
      GetPathFromRawMemory(reinterpret_cast<void*>(path_mem->vaddr()), message->path_memory_size());

  zx::channel storage_channel;
  constexpr bool kNoCreate = false;
  zx_status_t status = GetStorageDirectory(path.parent_path(), kNoCreate, &storage_channel);
  if (status != ZX_OK) {
    LOG(ERROR, "failed to get storage directory (status %d)", status);
    message->set_return_code(TEEC_ERROR_BAD_STATE);
    return status;
  }

  std::string filename = path.filename().string();
  auto result = fuchsia_io::Directory::Call::Unlink(zx::unowned_channel(storage_channel),
                                                    fidl::unowned_str(filename));
  status = result.status();
  zx_status_t io_status = result->s;
  if (status != ZX_OK || io_status != ZX_OK) {
    LOG(ERROR, "failed to remove file (FIDL status: %d, IO status: %d)", status, io_status);
    message->set_return_code(TEEC_ERROR_GENERIC);
    return status;
  }

  message->set_return_code(TEEC_SUCCESS);
  return ZX_OK;
}

zx_status_t OpteeClient::HandleRpcCommandFileSystemRenameFile(
    RenameFileFileSystemRpcMessage* message) {
  ZX_DEBUG_ASSERT(message != nullptr);

  LOG(TRACE, "received RPC to rename file");

  std::optional<SharedMemoryView> old_path_mem = GetMemoryReference(
      FindSharedMemory(message->old_file_name_memory_identifier()),
      message->old_file_name_memory_paddr(), message->old_file_name_memory_size());
  if (!old_path_mem.has_value()) {
    message->set_return_code(TEEC_ERROR_BAD_PARAMETERS);
    return ZX_ERR_INVALID_ARGS;
  }

  std::filesystem::path old_path = GetPathFromRawMemory(
      reinterpret_cast<void*>(old_path_mem->vaddr()), message->old_file_name_memory_size());
  std::string old_name = old_path.filename().string();

  std::optional<SharedMemoryView> new_path_mem = GetMemoryReference(
      FindSharedMemory(message->new_file_name_memory_identifier()),
      message->new_file_name_memory_paddr(), message->new_file_name_memory_size());
  if (!new_path_mem.has_value()) {
    message->set_return_code(TEEC_ERROR_BAD_PARAMETERS);
    return ZX_ERR_INVALID_ARGS;
  }

  std::filesystem::path new_path = GetPathFromRawMemory(
      reinterpret_cast<void*>(new_path_mem->vaddr()), message->new_file_name_memory_size());
  std::string new_name = new_path.filename().string();

  zx::channel new_storage_channel;
  constexpr bool kNoCreate = false;
  zx_status_t status = GetStorageDirectory(new_path.parent_path(), kNoCreate, &new_storage_channel);
  if (status != ZX_OK) {
    message->set_return_code(TEEC_ERROR_BAD_STATE);
    return status;
  }

  if (!message->should_overwrite()) {
    zx::channel destination_channel;
    static constexpr uint32_t kCheckRenameFlags =
        fuchsia_io::wire::OPEN_RIGHT_READABLE | fuchsia_io::wire::OPEN_FLAG_DESCRIBE;
    static constexpr uint32_t kCheckRenameMode =
        fuchsia_io::wire::MODE_TYPE_FILE | fuchsia_io::wire::MODE_TYPE_DIRECTORY;
    status = OpenObjectInDirectory(zx::unowned_channel(new_storage_channel), kCheckRenameFlags,
                                   kCheckRenameMode, new_name, &destination_channel);
    if (status == ZX_OK) {
      // The file exists but shouldn't be overwritten
      LOG(INFO, "refusing to rename file to path that already exists with overwrite set to false");
      message->set_return_code(TEEC_ERROR_ACCESS_CONFLICT);
      return ZX_OK;
    } else if (status != ZX_ERR_NOT_FOUND) {
      LOG(ERROR, "could not check file existence before renaming (status %d)", status);
      message->set_return_code(TEEC_ERROR_GENERIC);
      return status;
    }
  }

  zx::channel old_storage_channel;
  status = GetStorageDirectory(old_path.parent_path(), kNoCreate, &old_storage_channel);
  if (status != ZX_OK) {
    message->set_return_code(TEEC_ERROR_BAD_STATE);
    return status;
  }

  auto token_result =
      fuchsia_io::Directory::Call::GetToken(zx::unowned_channel(new_storage_channel));
  status = token_result.status();
  auto io_status = token_result->s;
  if (status != ZX_OK || io_status != ZX_OK) {
    LOG(ERROR,
        "could not get destination directory's storage token (FIDL status: %d, IO status: %d)",
        status, io_status);
    message->set_return_code(TEEC_ERROR_GENERIC);
    return status;
  }

  auto rename_result = fuchsia_io::Directory::Call::Rename(
      zx::unowned_channel(old_storage_channel), fidl::unowned_str(old_name),
      std::move(token_result->token), fidl::unowned_str(new_name));
  status = rename_result.status();
  io_status = rename_result->s;
  if (status != ZX_OK || io_status != ZX_OK) {
    LOG(ERROR, "failed to rename file (FIDL status: %d, IO status: %d)", status, io_status);
    message->set_return_code(TEEC_ERROR_GENERIC);
    return status;
  }

  message->set_return_code(TEEC_SUCCESS);
  return ZX_OK;
}

}  // namespace optee
