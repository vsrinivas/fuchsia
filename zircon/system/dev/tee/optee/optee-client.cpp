// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <libgen.h>
#include <string>
#include <utility>
#include <vector>

#include <ddk/debug.h>
#include <fbl/string_buffer.h>
#include <fuchsia/io/c/fidl.h>
#include <fuchsia/tee/manager/c/fidl.h>
#include <lib/fidl-utils/bind.h>
#include <lib/fidl/coding.h>
#include <lib/zx/handle.h>
#include <lib/zx/time.h>
#include <lib/zx/vmo.h>
#include <tee-client-api/tee-client-types.h>
#include <zircon/time.h>

#include "optee-client.h"
#include "optee-smc.h"

namespace {
// RFC 4122 specification dictates a UUID is of the form xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
constexpr const char* kUuidNameFormat = "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x";
constexpr size_t kUuidNameLength = 36;

constexpr const char kTaFileExtension[] = ".ta";

// The length of a path to a trusted app consists of its UUID and file extension
// Subtracting 1 from sizeof(char[])s to account for the terminating null character.
constexpr size_t kTaPathLength = kUuidNameLength + (sizeof(kTaFileExtension) - 1u);

template <typename SRC_T, typename DST_T>
static constexpr typename std::enable_if<
    std::is_unsigned<SRC_T>::value &&
    std::is_unsigned<DST_T>::value>::type
SplitInto32BitParts(SRC_T src, DST_T* dst_hi, DST_T* dst_lo) {
    static_assert(sizeof(SRC_T) == 8, "Type SRC_T should be 64 bits!");
    static_assert(sizeof(DST_T) >= 4, "Type DST_T should be at least 32 bits!");
    ZX_DEBUG_ASSERT(dst_hi != nullptr);
    ZX_DEBUG_ASSERT(dst_lo != nullptr);
    *dst_hi = static_cast<DST_T>(src >> 32);
    *dst_lo = static_cast<DST_T>(static_cast<uint32_t>(src));
}

template <typename SRC_T, typename DST_T>
static constexpr typename std::enable_if<
    std::is_unsigned<SRC_T>::value &&
    std::is_unsigned<DST_T>::value>::type
JoinFrom32BitParts(SRC_T src_hi, SRC_T src_lo, DST_T* dst) {
    static_assert(sizeof(SRC_T) >= 4, "Type SRC_T should be at least 32 bits!");
    static_assert(sizeof(DST_T) >= 8, "Type DST_T should be at least 64-bits!");
    ZX_DEBUG_ASSERT(dst != nullptr);
    *dst = (static_cast<DST_T>(src_hi) << 32) | static_cast<DST_T>(static_cast<uint32_t>(src_lo));
}

// Builds a UUID string from a TEEC_UUID, formatting as per the RFC 4122 specification.
static fbl::StringBuffer<kUuidNameLength> BuildUuidString(const TEEC_UUID& ta_uuid) {
    fbl::StringBuffer<kUuidNameLength> buf;

    buf.AppendPrintf(kUuidNameFormat,
                     ta_uuid.timeLow,
                     ta_uuid.timeMid,
                     ta_uuid.timeHiAndVersion,
                     ta_uuid.clockSeqAndNode[0],
                     ta_uuid.clockSeqAndNode[1],
                     ta_uuid.clockSeqAndNode[2],
                     ta_uuid.clockSeqAndNode[3],
                     ta_uuid.clockSeqAndNode[4],
                     ta_uuid.clockSeqAndNode[5],
                     ta_uuid.clockSeqAndNode[6],
                     ta_uuid.clockSeqAndNode[7]);
    return buf;
}

// Builds the expected path to a trusted application, formatting the file name per the RFC 4122
// specification.
static fbl::StringBuffer<kTaPathLength> BuildTaPath(const TEEC_UUID& ta_uuid) {
    fbl::StringBuffer<kTaPathLength> buf;

    buf.AppendPrintf(kUuidNameFormat,
                     ta_uuid.timeLow,
                     ta_uuid.timeMid,
                     ta_uuid.timeHiAndVersion,
                     ta_uuid.clockSeqAndNode[0],
                     ta_uuid.clockSeqAndNode[1],
                     ta_uuid.clockSeqAndNode[2],
                     ta_uuid.clockSeqAndNode[3],
                     ta_uuid.clockSeqAndNode[4],
                     ta_uuid.clockSeqAndNode[5],
                     ta_uuid.clockSeqAndNode[6],
                     ta_uuid.clockSeqAndNode[7]);
    buf.Append(kTaFileExtension);

    return buf;
}

static zx_status_t ConvertOpteeToZxResult(uint32_t optee_return_code, uint32_t optee_return_origin,
                                          fuchsia_tee_OpResult* zx_result) {
    ZX_DEBUG_ASSERT(zx_result != nullptr);

    // Do a quick check of the return origin to make sure we can map it to one
    // of our FIDL values. If none match, return a communication error instead.
    switch (optee_return_origin) {
    case TEEC_ORIGIN_COMMS:
        zx_result->return_code = optee_return_code;
        zx_result->return_origin = fuchsia_tee_ReturnOrigin_COMMUNICATION;
        break;
    case TEEC_ORIGIN_TEE:
        zx_result->return_code = optee_return_code;
        zx_result->return_origin = fuchsia_tee_ReturnOrigin_TRUSTED_OS;
        break;
    case TEEC_ORIGIN_TRUSTED_APP:
        zx_result->return_code = optee_return_code;
        zx_result->return_origin = fuchsia_tee_ReturnOrigin_TRUSTED_APPLICATION;
        break;
    default:
        zxlogf(ERROR, "optee: optee returned an invalid return origin (%" PRIu32 ")\n",
               optee_return_origin);
        zx_result->return_code = TEEC_ERROR_COMMUNICATION;
        zx_result->return_origin = fuchsia_tee_ReturnOrigin_COMMUNICATION;
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
//
// TODO(godtamit): Transition to receiving an event normally once this client code can use FIDL C++
// generated bindings that support events directly.
static zx_status_t AwaitIoOnOpenStatus(const zx::channel& channel) {
    zx_signals_t observed_signals = 0;
    zx_status_t status = channel.wait_one(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
                                          zx::time::infinite(), &observed_signals);
    if (status != ZX_OK) {
        zxlogf(ERROR, "optee::%s: failed to wait on channel (status: %d)\n", __FUNCTION__, status);
        return status;
    }

    // Intentionally allow `ZX_CHANNEL_PEER_CLOSED` to take precedence over `ZX_CHANNEL_READABLE`
    // since it indicates an error occurred.
    if ((observed_signals & ZX_CHANNEL_PEER_CLOSED) == ZX_CHANNEL_PEER_CLOSED) {
        zxlogf(ERROR, "optee::%s: channel closed\n", __FUNCTION__);

        // TODO(godtamit): check for an epitaph here once `fuchsia.io` supports it
        return ZX_ERR_NOT_FOUND;
    }

    // Sanity check to make sure `ZX_CHANNEL_READABLE` was the signal observed
    ZX_DEBUG_ASSERT((observed_signals & ZX_CHANNEL_READABLE) == ZX_CHANNEL_READABLE);

    // Test to see how big the message is
    uint32_t actual_bytes;
    zx::handle handle;
    status = channel.read(0,             // flags
                          nullptr,       // bytes
                          nullptr,       // handles
                          0,             // num_bytes
                          0,             // num_handles
                          &actual_bytes, // actual_bytes
                          nullptr);      // actual_handles
    if (status != ZX_ERR_BUFFER_TOO_SMALL) {
        zxlogf(ERROR,
               "optee::%s: received unexpecting error while testing for channel message size "
               "(status: %d)\n",
               __FUNCTION__, status);
        return status == ZX_OK ? ZX_ERR_INTERNAL : status;
    }

    uint32_t buffer_size = actual_bytes;
    std::unique_ptr<uint8_t[]> buffer(new uint8_t[static_cast<size_t>(buffer_size)]);
    status = channel.read(0,                              // flags
                          buffer.get(),                   // bytes
                          handle.reset_and_get_address(), // handles
                          buffer_size,                    // num_bytes
                          1,                              // num_handles
                          &actual_bytes,                  // actual_bytes
                          nullptr);                       // actual_handles
    if (status != ZX_OK) {
        zxlogf(ERROR,
               "optee::%s: received unexpecting error while reading channel message (status: %d)\n",
               __FUNCTION__, status);
        return status;
    }

    auto header = reinterpret_cast<fidl_message_header_t*>(buffer.get());
    if (header->ordinal != fuchsia_io_NodeOnOpenOrdinal) {
        // The `OnOpen` event should be the first event fired. See the function description for
        // preconditions and details.
        zxlogf(ERROR, "optee::%s: received unexpected message ordinal %x\n",
               __FUNCTION__, header->ordinal);
        return ZX_ERR_PROTOCOL_NOT_SUPPORTED;
    }

    const char* err = nullptr;
    status =
        fidl_decode(&fuchsia_io_NodeOnOpenEventTable, buffer.get(), actual_bytes, nullptr, 0, &err);
    if (status != ZX_OK) {
        zxlogf(ERROR, "optee::%s: failed to decode fuchsia.io.Node/OnOpen event: %s (status: %d)\n",
               __FUNCTION__, err != nullptr ? err : "", status);
        return status;
    }

    auto on_open_event = reinterpret_cast<fuchsia_io_NodeOnOpenEvent*>(buffer.get());
    return on_open_event->s;
}

// Calls `fuchsia.io.Directory/Open` on a channel and awaits the result.
static zx_status_t OpenObjectInDirectory(const zx::channel& root_channel,
                                         uint32_t flags,
                                         uint32_t mode,
                                         std::string path,
                                         zx::channel* out_channel_node) {
    ZX_DEBUG_ASSERT(out_channel_node != nullptr);

    // Ensure `OPEN_FLAG_DESCRIBE` is passed
    flags |= fuchsia_io_OPEN_FLAG_DESCRIBE;

    // Create temporary channel ends to make FIDL call
    zx::channel channel_client_end;
    zx::channel channel_server_end;
    zx_status_t status = zx::channel::create(0, &channel_client_end, &channel_server_end);
    if (status != ZX_OK) {
        zxlogf(ERROR, "optee::%s: failed to create channel pair (status: %d)\n",
               __FUNCTION__, status);
        return status;
    }

    status = fuchsia_io_DirectoryOpen(root_channel.get(),            // _channel
                                      flags,                         // flags
                                      mode,                          // mode
                                      path.data(),                   // path_data
                                      path.size(),                   // path_size
                                      channel_server_end.release()); // object
    if (status != ZX_OK) {
        zxlogf(ERROR, "optee::%s: could not call fuchsia.io.Directory/Open (status: %d)\n",
               __FUNCTION__, status);
        return status;
    }

    status = AwaitIoOnOpenStatus(channel_client_end);
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
static zx_status_t RecursivelyWalkPath(const zx::unowned_channel& root_channel,
                                       std::filesystem::path path,
                                       zx::channel* out_node_channel) {
    static_assert((kOpenFlags & fuchsia_io_OPEN_FLAG_NOT_DIRECTORY) == 0,
                  "kOpenFlags must not include fuchsia_io_OPEN_FLAG_NOT_DIRECTORY");
    ZX_DEBUG_ASSERT(root_channel->is_valid());
    ZX_DEBUG_ASSERT(!path.empty());
    ZX_DEBUG_ASSERT(out_node_channel != nullptr);

    zx_status_t status;
    zx::channel result_channel;

    if (path == std::filesystem::path(".")) {
        // If the path is lexicographically equivalent to the (relative) root directory, clone the
        // root channel instead of opening the path
        zx::channel server_channel;
        status = zx::channel::create(0, &result_channel, &server_channel);
        if (status != ZX_OK) {
            return status;
        }

        status = fuchsia_io_DirectoryClone(root_channel->get(),
                                           fuchsia_io_CLONE_FLAG_SAME_RIGHTS,
                                           server_channel.release());
        if (status != ZX_OK) {
            return status;
        }
    } else {
        zx::unowned_channel current_channel(root_channel);
        for (const auto& component : path) {
            zx::channel temporary_channel;
            static constexpr uint32_t kOpenMode = fuchsia_io_MODE_TYPE_DIRECTORY;
            status = OpenObjectInDirectory(*current_channel,
                                           kOpenFlags,
                                           kOpenMode,
                                           component.string(),
                                           &temporary_channel);
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
    static constexpr uint32_t kCreateFlags = fuchsia_io_OPEN_RIGHT_READABLE |
                                             fuchsia_io_OPEN_RIGHT_WRITABLE |
                                             fuchsia_io_OPEN_FLAG_CREATE |
                                             fuchsia_io_OPEN_FLAG_DIRECTORY;
    return RecursivelyWalkPath<kCreateFlags>(std::forward<Args>(args)...);
}

template <typename... Args>
static inline zx_status_t OpenDirectory(Args&&... args) {
    static constexpr uint32_t kOpenFlags = fuchsia_io_OPEN_RIGHT_READABLE |
                                           fuchsia_io_OPEN_RIGHT_WRITABLE |
                                           fuchsia_io_OPEN_FLAG_DIRECTORY;
    return RecursivelyWalkPath<kOpenFlags>(std::forward<Args>(args)...);
}

} // namespace

namespace optee {

fuchsia_tee_Device_ops_t OpteeClient::kFidlOps = {
    .GetOsInfo = fidl::Binder<OpteeClient>::BindMember<&OpteeClient::GetOsInfo>,
    .OpenSession = fidl::Binder<OpteeClient>::BindMember<&OpteeClient::OpenSession>,
    .InvokeCommand = fidl::Binder<OpteeClient>::BindMember<&OpteeClient::InvokeCommand>,

    // Use the BindMember version disambiguates the overloaded CloseSession() by function signature
    .CloseSession = fidl::Binder<OpteeClient>::BindMember<zx_status_t(uint32_t, fidl_txn_t*),
                                                          &OpteeClient::CloseSession>,
};

zx_status_t OpteeClient::DdkClose(uint32_t flags) {
    controller_->RemoveClient(this);
    return ZX_OK;
}

void OpteeClient::DdkRelease() {
    // devmgr has given up ownership, so we must clean ourself up.
    //
    // Try and cleanly close all sessions
    std::vector<uint32_t> session_ids(open_sessions_.size());
    session_ids.assign(open_sessions_.begin(), open_sessions_.end());

    for (uint32_t id : session_ids) {
        // Regardless of CloseSession response, continue closing all other sessions
        __UNUSED zx_status_t status = CloseSession(id);
    }

    // Clear memory list, which releases all memory blocks back to their respective pools
    allocated_shared_memory_.clear();

    delete this;
}

zx_status_t OpteeClient::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
    if (needs_to_close_) {
        // The underlying channel is owned by the devhost and thus we do not need to directly close
        // it. This check exists for the scenario where we are in the process of unbinding the
        // parent device and cannot fulfill any requests any more. The underlying channel will be
        // closed by devhost once the unbind is complete.
        return ZX_ERR_PEER_CLOSED;
    }
    return fuchsia_tee_Device_dispatch(this, txn, msg, &kFidlOps);
}

zx_status_t OpteeClient::GetOsInfo(fidl_txn_t* txn) const {
    return controller_->GetOsInfo(txn);
}

zx_status_t OpteeClient::OpenSession(const fuchsia_tee_Uuid* trusted_app,
                                     const fuchsia_tee_ParameterSet* parameter_set,
                                     fidl_txn_t* txn) {
    constexpr uint32_t kInvalidSession = 0;

    ZX_DEBUG_ASSERT(trusted_app != nullptr);
    ZX_DEBUG_ASSERT(parameter_set != nullptr);

    fuchsia_tee_OpResult result = {};

    Uuid ta_uuid{*trusted_app};

    OpenSessionMessage message{controller_->driver_pool(), controller_->client_pool(),
                               ta_uuid, *parameter_set};

    if (!message.is_valid()) {
        result.return_code = TEEC_ERROR_COMMUNICATION;
        result.return_origin = fuchsia_tee_ReturnOrigin_COMMUNICATION;
        return fuchsia_tee_DeviceOpenSession_reply(txn, kInvalidSession, &result);
    }

    uint32_t call_code = controller_->CallWithMessage(
        message, fbl::BindMember(this, &OpteeClient::HandleRpc));
    if (call_code != kReturnOk) {
        result.return_code = TEEC_ERROR_COMMUNICATION;
        result.return_origin = fuchsia_tee_ReturnOrigin_COMMUNICATION;
        return fuchsia_tee_DeviceOpenSession_reply(txn, kInvalidSession, &result);
    }

    zxlogf(SPEW, "optee: OpenSession returned 0x%" PRIx32 " 0x%" PRIx32 " 0x%" PRIx32 "\n",
           call_code, message.return_code(), message.return_origin());

    if (ConvertOpteeToZxResult(message.return_code(), message.return_origin(), &result) != ZX_OK) {
        return fuchsia_tee_DeviceOpenSession_reply(txn, kInvalidSession, &result);
    }

    if (message.CreateOutputParameterSet(&result.parameter_set) != ZX_OK) {
        // Since we failed to parse the output parameters, let's close the session and report error.
        // It is okay that the session id is not in the session list.
        CloseSession(message.session_id());
        result.return_code = TEEC_ERROR_COMMUNICATION;
        result.return_origin = fuchsia_tee_ReturnOrigin_COMMUNICATION;
        return fuchsia_tee_DeviceOpenSession_reply(txn, kInvalidSession, &result);
    }

    open_sessions_.insert(message.session_id());

    return fuchsia_tee_DeviceOpenSession_reply(txn, message.session_id(), &result);
}

zx_status_t OpteeClient::InvokeCommand(uint32_t session_id, uint32_t command_id,
                                       const fuchsia_tee_ParameterSet* parameter_set,
                                       fidl_txn_t* txn) {
    ZX_DEBUG_ASSERT(parameter_set != nullptr);

    fuchsia_tee_OpResult result = {};

    if (open_sessions_.find(session_id) == open_sessions_.end()) {
        result.return_code = TEEC_ERROR_BAD_STATE;
        result.return_origin = fuchsia_tee_ReturnOrigin_COMMUNICATION;
        return fuchsia_tee_DeviceInvokeCommand_reply(txn, &result);
    }

    InvokeCommandMessage message{controller_->driver_pool(), controller_->client_pool(),
                                 session_id, command_id, *parameter_set};

    if (!message.is_valid()) {
        result.return_code = TEEC_ERROR_COMMUNICATION;
        result.return_origin = fuchsia_tee_ReturnOrigin_COMMUNICATION;
        return fuchsia_tee_DeviceInvokeCommand_reply(txn, &result);
    }

    uint32_t call_code = controller_->CallWithMessage(
        message, fbl::BindMember(this, &OpteeClient::HandleRpc));
    if (call_code != kReturnOk) {
        result.return_code = TEEC_ERROR_COMMUNICATION;
        result.return_origin = fuchsia_tee_ReturnOrigin_COMMUNICATION;
        return fuchsia_tee_DeviceInvokeCommand_reply(txn, &result);
    }

    zxlogf(SPEW, "optee: InvokeCommand returned 0x%" PRIx32 " 0x%" PRIx32 " 0x%" PRIx32 "\n",
           call_code, message.return_code(), message.return_origin());

    if (ConvertOpteeToZxResult(message.return_code(), message.return_origin(), &result) != ZX_OK) {
        return fuchsia_tee_DeviceInvokeCommand_reply(txn, &result);
    }

    if (message.CreateOutputParameterSet(&result.parameter_set) != ZX_OK) {
        result.return_code = TEEC_ERROR_COMMUNICATION;
        result.return_origin = fuchsia_tee_ReturnOrigin_COMMUNICATION;
        return fuchsia_tee_DeviceInvokeCommand_reply(txn, &result);
    }

    return fuchsia_tee_DeviceInvokeCommand_reply(txn, &result);
}

zx_status_t OpteeClient::CloseSession(uint32_t session_id) {
    CloseSessionMessage message{controller_->driver_pool(), session_id};

    if (!message.is_valid()) {
        return ZX_ERR_NO_RESOURCES;
    }

    uint32_t call_code = controller_->CallWithMessage(
        message, fbl::BindMember(this, &OpteeClient::HandleRpc));

    if (call_code == kReturnOk) {
        open_sessions_.erase(session_id);
    }

    zxlogf(SPEW, "optee: CloseSession returned %" PRIx32 " %" PRIx32 " %" PRIx32 "\n",
           call_code, message.return_code(), message.return_origin());
    return ZX_OK;
}

zx_status_t OpteeClient::CloseSession(uint32_t session_id,
                                      fidl_txn_t* txn) {
    zx_status_t status = CloseSession(session_id);
    if (status != ZX_OK) {
        return status;
    }

    return fuchsia_tee_DeviceCloseSession_reply(txn);
}

template <typename SharedMemoryPoolTraits>
zx_status_t OpteeClient::AllocateSharedMemory(size_t size,
                                              SharedMemoryPool<SharedMemoryPoolTraits>* memory_pool,
                                              zx_paddr_t* out_phys_addr,
                                              uint64_t* out_mem_id) {
    ZX_DEBUG_ASSERT(memory_pool != nullptr);
    ZX_DEBUG_ASSERT(out_phys_addr != nullptr);
    ZX_DEBUG_ASSERT(out_mem_id != nullptr);

    // Set these to 0 and overwrite, if necessary, on success path
    *out_phys_addr = 0;
    *out_mem_id = 0;

    if (size == 0) {
        return ZX_ERR_INVALID_ARGS;
    }

    fbl::unique_ptr<SharedMemory> sh_mem;
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
    return allocated_shared_memory_.find_if(
        [mem_id_ptr_val](auto& item) {
            return mem_id_ptr_val == reinterpret_cast<uintptr_t>(&item);
        });
}

std::optional<SharedMemoryView> OpteeClient::GetMemoryReference(SharedMemoryList::iterator mem_iter,
                                                                zx_paddr_t base_paddr,
                                                                size_t size) {

    std::optional<SharedMemoryView> result;
    if (!mem_iter.IsValid() ||
        !(result = mem_iter->SliceByPaddr(base_paddr, base_paddr + size)).has_value()) {
        zxlogf(ERROR, "optee: received invalid shared memory region reference\n");
    }
    return result;
}

zx_status_t OpteeClient::GetRootStorageChannel(zx::unowned_channel* out_root_channel) {
    ZX_DEBUG_ASSERT(out_root_channel != nullptr);

    if (!service_provider_channel_.is_valid()) {
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

    status = fuchsia_tee_manager_ServiceProviderRequestPersistentStorage(
        service_provider_channel_.get(),
        server_channel.release());
    if (status != ZX_OK) {
        return status;
    }

    root_storage_channel_ = std::move(client_channel);
    *out_root_channel = zx::unowned_channel(root_storage_channel_);
    return ZX_OK;
}

zx_status_t OpteeClient::GetStorageDirectory(std::filesystem::path path,
                                             bool create,
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

std::optional<zx::unowned_channel>
OpteeClient::GetFileSystemObjectChannel(uint64_t identifier) {
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
                                              controller_->driver_pool(),
                                              &paddr,
                                              &mem_id);
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
    RpcMessage message(&msg_mem);
    if (!message.is_valid()) {
        return ZX_ERR_INVALID_ARGS;
    }

    // Mark that the return code will originate from driver
    message.set_return_origin(TEEC_ORIGIN_COMMS);

    switch (message.command()) {
    case RpcMessage::Command::kLoadTa: {
        LoadTaRpcMessage load_ta_msg(std::move(message));
        if (!load_ta_msg.is_valid()) {
            return ZX_ERR_INVALID_ARGS;
        }
        return HandleRpcCommandLoadTa(&load_ta_msg);
    }
    case RpcMessage::Command::kAccessFileSystem: {
        FileSystemRpcMessage fs_msg(std::move(message));
        if (!fs_msg.is_valid()) {
            return ZX_ERR_INVALID_ARGS;
        }
        return HandleRpcCommandFileSystem(std::move(fs_msg));
    }
    case RpcMessage::Command::kGetTime: {
        GetTimeRpcMessage get_time_msg(std::move(message));
        if (!get_time_msg.is_valid()) {
            return ZX_ERR_INVALID_ARGS;
        }
        return HandleRpcCommandGetTime(&get_time_msg);
    }
    case RpcMessage::Command::kWaitQueue:
        zxlogf(ERROR, "optee: RPC command wait queue recognized but not implemented\n");
        return ZX_ERR_NOT_SUPPORTED;
    case RpcMessage::Command::kSuspend:
        zxlogf(ERROR, "optee: RPC command to suspend recognized but not implemented\n");
        return ZX_ERR_NOT_SUPPORTED;
    case RpcMessage::Command::kAllocateMemory: {
        AllocateMemoryRpcMessage alloc_mem_msg(std::move(message));
        if (!alloc_mem_msg.is_valid()) {
            return ZX_ERR_INVALID_ARGS;
        }
        return HandleRpcCommandAllocateMemory(&alloc_mem_msg);
    }
    case RpcMessage::Command::kFreeMemory: {
        FreeMemoryRpcMessage free_mem_msg(std::move(message));
        if (!free_mem_msg.is_valid()) {
            return ZX_ERR_INVALID_ARGS;
        }
        return HandleRpcCommandFreeMemory(&free_mem_msg);
    }
    case RpcMessage::Command::kPerformSocketIo:
        zxlogf(ERROR, "optee: RPC command to perform socket IO recognized but not implemented\n");
        message.set_return_code(TEEC_ERROR_NOT_SUPPORTED);
        return ZX_OK;
    case RpcMessage::Command::kAccessReplayProtectedMemoryBlock:
    case RpcMessage::Command::kAccessSqlFileSystem:
    case RpcMessage::Command::kLoadGprof:
        zxlogf(INFO, "optee: received unsupported RPC command\n");
        message.set_return_code(TEEC_ERROR_NOT_SUPPORTED);
        return ZX_OK;
    default:
        zxlogf(ERROR,
               "optee: unrecognized command passed to RPC 0x%" PRIu32 "\n",
               message.command());
        message.set_return_code(TEEC_ERROR_NOT_SUPPORTED);
        return ZX_ERR_NOT_SUPPORTED;
    }
}

zx_status_t OpteeClient::HandleRpcCommandLoadTa(LoadTaRpcMessage* message) {
    ZX_DEBUG_ASSERT(message != nullptr);
    ZX_DEBUG_ASSERT(message->is_valid());

    // Try to find the SharedMemory based on the memory id
    std::optional<SharedMemoryView> out_ta_mem; // Where to write the TA in memory

    if (message->memory_reference_id() != 0) {
        out_ta_mem = GetMemoryReference(FindSharedMemory(message->memory_reference_id()),
                                        message->memory_reference_paddr(),
                                        message->memory_reference_size());
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
    zx_status_t status = load_firmware(controller_->zxdev(),
                                       ta_path.data(),
                                       ta_vmo.reset_and_get_address(),
                                       &ta_size);

    if (status != ZX_OK) {
        if (status == ZX_ERR_NOT_FOUND) {
            zxlogf(ERROR, "optee: could not find trusted app %s!\n", ta_path.data());
            message->set_return_code(TEEC_ERROR_ITEM_NOT_FOUND);
        } else {
            zxlogf(ERROR, "optee: error loading trusted app %s!\n", ta_path.data());
            message->set_return_code(TEEC_ERROR_GENERIC);
        }

        return status;
    } else if (ta_size == 0) {
        zxlogf(ERROR, "optee: loaded trusted app %s with unexpected size!\n", ta_path.data());
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
        zxlogf(ERROR, "optee: failed to copy trusted app from VMO to shared memory!\n");
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

zx_status_t OpteeClient::HandleRpcCommandGetTime(GetTimeRpcMessage* message) {
    ZX_DEBUG_ASSERT(message->is_valid());

    // Mark that the return code will originate from driver
    message->set_return_origin(TEEC_ORIGIN_COMMS);

    zx::time_utc now;
    zx_status_t status = zx::clock::get(&now);
    if (status != ZX_OK) {
        message->set_return_code(TEEC_ERROR_GENERIC);
        return status;
    }

    static constexpr zx::duration kDurationSecond = zx::sec(1);
    static constexpr zx::time_utc kUtcEpoch = zx::time_utc(0);

    zx::duration now_since_epoch = now - kUtcEpoch;
    auto seconds = static_cast<uint64_t>(now_since_epoch / kDurationSecond);
    auto ns_remainder = static_cast<uint64_t>(now_since_epoch % kDurationSecond);

    message->set_output_seconds(seconds);
    message->set_output_nanoseconds(ns_remainder);
    message->set_return_code(TEEC_SUCCESS);

    return ZX_OK;
}

zx_status_t OpteeClient::HandleRpcCommandAllocateMemory(AllocateMemoryRpcMessage* message) {
    ZX_DEBUG_ASSERT(message != nullptr);
    ZX_DEBUG_ASSERT(message->is_valid());

    if (message->memory_type() == SharedMemoryType::kGlobal) {
        zxlogf(ERROR, "optee: implementation currently does not support global shared memory!\n");
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
    ZX_DEBUG_ASSERT(message->is_valid());

    if (message->memory_type() == SharedMemoryType::kGlobal) {
        zxlogf(ERROR, "optee: implementation currently does not support global shared memory!\n");
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
    ZX_DEBUG_ASSERT(message.is_valid());

    // Mark that the return code will originate from driver
    message.set_return_origin(TEEC_ORIGIN_COMMS);

    if (!service_provider_channel_.is_valid()) {
        // Client did not connect with a ServiceProvider so none of these RPCs can be serviced
        message.set_return_code(TEEC_ERROR_BAD_STATE);
        return ZX_ERR_UNAVAILABLE;
    }

    switch (message.file_system_command()) {
    case FileSystemRpcMessage::FileSystemCommand::kOpenFile: {
        OpenFileFileSystemRpcMessage open_file_msg(std::move(message));
        if (!open_file_msg.is_valid()) {
            return ZX_ERR_INVALID_ARGS;
        }
        return HandleRpcCommandFileSystemOpenFile(&open_file_msg);
    }
    case FileSystemRpcMessage::FileSystemCommand::kCreateFile: {
        CreateFileFileSystemRpcMessage create_file_msg(std::move(message));
        if (!create_file_msg.is_valid()) {
            return ZX_ERR_INVALID_ARGS;
        }
        return HandleRpcCommandFileSystemCreateFile(&create_file_msg);
    }
    case FileSystemRpcMessage::FileSystemCommand::kCloseFile: {
        CloseFileFileSystemRpcMessage close_file_msg(std::move(message));
        if (!close_file_msg.is_valid()) {
            return ZX_ERR_INVALID_ARGS;
        }
        return HandleRpcCommandFileSystemCloseFile(&close_file_msg);
    }
    case FileSystemRpcMessage::FileSystemCommand::kReadFile: {
        ReadFileFileSystemRpcMessage read_file_msg(std::move(message));
        if (!read_file_msg.is_valid()) {
            return ZX_ERR_INVALID_ARGS;
        }
        return HandleRpcCommandFileSystemReadFile(&read_file_msg);
    }
    case FileSystemRpcMessage::FileSystemCommand::kWriteFile: {
        WriteFileFileSystemRpcMessage write_file_msg(std::move(message));
        if (!write_file_msg.is_valid()) {
            return ZX_ERR_INVALID_ARGS;
        }
        return HandleRpcCommandFileSystemWriteFile(&write_file_msg);
    }
    case FileSystemRpcMessage::FileSystemCommand::kTruncateFile: {
        TruncateFileFileSystemRpcMessage truncate_file_msg(std::move(message));
        if (!truncate_file_msg.is_valid()) {
            return ZX_ERR_INVALID_ARGS;
        }
        return HandleRpcCommandFileSystemTruncateFile(&truncate_file_msg);
    }
    case FileSystemRpcMessage::FileSystemCommand::kRemoveFile: {
        RemoveFileFileSystemRpcMessage remove_file_msg(std::move(message));
        if (!remove_file_msg.is_valid()) {
            return ZX_ERR_INVALID_ARGS;
        }
        return HandleRpcCommandFileSystemRemoveFile(&remove_file_msg);
    }
    case FileSystemRpcMessage::FileSystemCommand::kRenameFile: {
        RenameFileFileSystemRpcMessage rename_file_msg(std::move(message));
        if (!rename_file_msg.is_valid()) {
            return ZX_ERR_INVALID_ARGS;
        }
        return HandleRpcCommandFileSystemRenameFile(&rename_file_msg);
    }
    case FileSystemRpcMessage::FileSystemCommand::kOpenDirectory:
        zxlogf(ERROR, "optee: RPC command to open directory recognized but not implemented\n");
        break;
    case FileSystemRpcMessage::FileSystemCommand::kCloseDirectory:
        zxlogf(ERROR, "optee: RPC command to close directory recognized but not implemented\n");
        break;
    case FileSystemRpcMessage::FileSystemCommand::kGetNextFileInDirectory:
        zxlogf(ERROR,
               "optee: RPC command to get next file in directory recognized but not implemented\n");
        break;
    }

    message.set_return_code(TEEC_ERROR_NOT_SUPPORTED);
    return ZX_OK;
}

zx_status_t OpteeClient::HandleRpcCommandFileSystemOpenFile(OpenFileFileSystemRpcMessage* message) {
    ZX_DEBUG_ASSERT(message != nullptr);
    ZX_DEBUG_ASSERT(message->is_valid());
    ZX_DEBUG_ASSERT(service_provider_channel_.is_valid());

    zxlogf(SPEW, "optee: received RPC to open file\n");

    SharedMemoryList::iterator mem_iter = FindSharedMemory(message->path_memory_identifier());
    std::optional<SharedMemoryView> path_mem = GetMemoryReference(mem_iter,
                                                                  message->path_memory_paddr(),
                                                                  message->path_memory_size());
    if (!path_mem.has_value()) {
        message->set_return_code(TEEC_ERROR_BAD_PARAMETERS);
        return ZX_ERR_INVALID_ARGS;
    }

    std::filesystem::path path = GetPathFromRawMemory(reinterpret_cast<void*>(path_mem->vaddr()),
                                                      message->path_memory_size());

    zx::channel storage_channel;
    constexpr bool kNoCreate = false;
    zx_status_t status = GetStorageDirectory(path.parent_path(), kNoCreate, &storage_channel);
    if (status != ZX_OK) {
        message->set_return_code(TEEC_ERROR_BAD_STATE);
        return status;
    }

    zx::channel file_channel;
    static constexpr uint32_t kOpenFlags = fuchsia_io_OPEN_RIGHT_READABLE |
                                           fuchsia_io_OPEN_RIGHT_WRITABLE |
                                           fuchsia_io_OPEN_FLAG_NOT_DIRECTORY |
                                           fuchsia_io_OPEN_FLAG_DESCRIBE;
    static constexpr uint32_t kOpenMode = fuchsia_io_MODE_TYPE_FILE;
    status = OpenObjectInDirectory(storage_channel, kOpenFlags, kOpenMode, path.filename().string(),
                                   &file_channel);
    if (status != ZX_OK) {
        zxlogf(ERROR, "optee::%s: unable to open file (status: %d)\n", __FUNCTION__, status);
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
    ZX_DEBUG_ASSERT(message->is_valid());

    zxlogf(SPEW, "optee: received RPC to create file\n");

    std::optional<SharedMemoryView> path_mem = GetMemoryReference(
        FindSharedMemory(message->path_memory_identifier()),
        message->path_memory_paddr(),
        message->path_memory_size());
    if (!path_mem.has_value()) {
        message->set_return_code(TEEC_ERROR_BAD_PARAMETERS);
        return ZX_ERR_INVALID_ARGS;
    }

    std::filesystem::path path = GetPathFromRawMemory(reinterpret_cast<void*>(path_mem->vaddr()),
                                                      message->path_memory_size());

    zx::channel storage_channel;
    constexpr bool kCreate = true;
    zx_status_t status = GetStorageDirectory(path.parent_path(), kCreate, &storage_channel);
    if (status != ZX_OK) {
        message->set_return_code(TEEC_ERROR_BAD_STATE);
        return status;
    }

    zx::channel file_channel;
    static constexpr uint32_t kCreateFlags = fuchsia_io_OPEN_RIGHT_READABLE |
                                             fuchsia_io_OPEN_RIGHT_WRITABLE |
                                             fuchsia_io_OPEN_FLAG_CREATE |
                                             fuchsia_io_OPEN_FLAG_CREATE_IF_ABSENT |
                                             fuchsia_io_OPEN_FLAG_DESCRIBE;
    static constexpr uint32_t kCreateMode = fuchsia_io_MODE_TYPE_FILE;
    status = OpenObjectInDirectory(storage_channel, kCreateFlags, kCreateMode,
                                   path.filename().string(), &file_channel);
    if (status != ZX_OK) {
        zxlogf(ERROR, "optee::%s: unable to create file (status: %d)\n", __FUNCTION__, status);
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
    ZX_DEBUG_ASSERT(message->is_valid());

    zxlogf(SPEW, "optee: received RPC to close file\n");

    if (!UntrackFileSystemObject(message->file_system_object_identifier())) {
        zxlogf(ERROR, "optee: could not find the requested file to close\n");
        message->set_return_code(TEEC_ERROR_ITEM_NOT_FOUND);
        return ZX_ERR_NOT_FOUND;
    }

    message->set_return_code(TEEC_SUCCESS);
    return ZX_OK;
}

zx_status_t OpteeClient::HandleRpcCommandFileSystemReadFile(ReadFileFileSystemRpcMessage* message) {
    ZX_DEBUG_ASSERT(message != nullptr);
    ZX_DEBUG_ASSERT(message->is_valid());

    zxlogf(SPEW, "optee: received RPC to read from file\n");

    auto maybe_file_channel = GetFileSystemObjectChannel(message->file_system_object_identifier());
    if (!maybe_file_channel.has_value()) {
        message->set_return_code(TEEC_ERROR_ITEM_NOT_FOUND);
        return ZX_ERR_NOT_FOUND;
    }

    zx::unowned_channel file_channel(std::move(*maybe_file_channel));

    std::optional<SharedMemoryView> buffer_mem = GetMemoryReference(
        FindSharedMemory(message->file_contents_memory_identifier()),
        message->file_contents_memory_paddr(),
        message->file_contents_memory_size());
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
    while (bytes_left > 0) {
        uint64_t read_chunk_request = std::min(bytes_left, fuchsia_io_MAX_BUF);
        uint64_t read_chunk_actual = 0;
        status = fuchsia_io_FileReadAt(file_channel->get(), // _channel
                                       read_chunk_request,  // count
                                       offset,              // offset
                                       &io_status,          // out_s
                                       buffer,              // data_buffer
                                       read_chunk_request,  // data_capacity
                                       &read_chunk_actual); // out_actual
        buffer += read_chunk_actual;
        offset += read_chunk_actual;
        bytes_left -= read_chunk_actual;
        bytes_read += read_chunk_actual;

        if (status != ZX_OK || io_status != ZX_OK) {
            zxlogf(ERROR, "optee::%s failed to read from file (FIDL status: %d, IO status: %d)\n",
                   __FUNCTION__, status, io_status);
            message->set_return_code(TEEC_ERROR_GENERIC);
            return status;
        }

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
    ZX_DEBUG_ASSERT(message->is_valid());

    zxlogf(SPEW, "optee: received RPC to write file\n");

    auto maybe_file_channel = GetFileSystemObjectChannel(message->file_system_object_identifier());
    if (!maybe_file_channel.has_value()) {
        message->set_return_code(TEEC_ERROR_ITEM_NOT_FOUND);
        return ZX_ERR_NOT_FOUND;
    }

    zx::unowned_channel file_channel(std::move(*maybe_file_channel));

    std::optional<SharedMemoryView> buffer_mem = GetMemoryReference(
        FindSharedMemory(message->file_contents_memory_identifier()),
        message->file_contents_memory_paddr(),
        message->file_contents_memory_size());
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
        uint64_t write_chunk_request = std::min(bytes_left, fuchsia_io_MAX_BUF);
        uint64_t write_chunk_actual = 0;
        status = fuchsia_io_FileWriteAt(file_channel->get(),  // _channel
                                        buffer,               // data_data
                                        write_chunk_request,  // data_count
                                        offset,               // offset
                                        &io_status,           // out_s
                                        &write_chunk_actual); // out_actual
        buffer += write_chunk_actual;
        offset += write_chunk_actual;
        bytes_left -= write_chunk_actual;

        if (status != ZX_OK || io_status != ZX_OK) {
            zxlogf(ERROR, "optee::%s failed to write to file (FIDL status: %d, IO status: %d)\n",
                   __FUNCTION__, status, io_status);
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
    ZX_DEBUG_ASSERT(message->is_valid());

    zxlogf(SPEW, "optee: received RPC to truncate file\n");

    auto maybe_file_channel = GetFileSystemObjectChannel(message->file_system_object_identifier());
    if (!maybe_file_channel.has_value()) {
        message->set_return_code(TEEC_ERROR_ITEM_NOT_FOUND);
        return ZX_ERR_NOT_FOUND;
    }

    zx::unowned_channel file_channel(std::move(*maybe_file_channel));

    zx_status_t io_status = ZX_OK;
    zx_status_t status = fuchsia_io_FileTruncate(file_channel->get(),         // _channel
                                                 message->target_file_size(), // length
                                                 &io_status);                 // out_s
    if (status != ZX_OK || io_status != ZX_OK) {
        zxlogf(ERROR, "optee::%s failed to truncate file (FIDL status: %d, IO status: %d)\n",
               __FUNCTION__, status, io_status);
        message->set_return_code(TEEC_ERROR_GENERIC);
        return status;
    }

    message->set_return_code(TEEC_SUCCESS);
    return ZX_OK;
}

zx_status_t OpteeClient::HandleRpcCommandFileSystemRemoveFile(
    RemoveFileFileSystemRpcMessage* message) {
    ZX_DEBUG_ASSERT(message != nullptr);
    ZX_DEBUG_ASSERT(message->is_valid());

    zxlogf(SPEW, "optee: received RPC to remove file\n");

    std::optional<SharedMemoryView> path_mem = GetMemoryReference(
        FindSharedMemory(message->path_memory_identifier()),
        message->path_memory_paddr(),
        message->path_memory_size());
    if (!path_mem.has_value()) {
        message->set_return_code(TEEC_ERROR_BAD_PARAMETERS);
        return ZX_ERR_INVALID_ARGS;
    }

    std::filesystem::path path = GetPathFromRawMemory(reinterpret_cast<void*>(path_mem->vaddr()),
                                                      message->path_memory_size());

    zx::channel storage_channel;
    constexpr bool kNoCreate = false;
    zx_status_t status = GetStorageDirectory(path.parent_path(), kNoCreate, &storage_channel);
    if (status != ZX_OK) {
        zxlogf(ERROR, "optee::%s: failed to get storage directory (status %d)\n",
               __FUNCTION__, status);
        message->set_return_code(TEEC_ERROR_BAD_STATE);
        return status;
    }

    zx_status_t io_status;
    std::string filename = path.filename().string();
    status = fuchsia_io_DirectoryUnlink(storage_channel.get(), filename.data(), filename.length(),
                                        &io_status);
    if (status != ZX_OK || io_status != ZX_OK) {
        zxlogf(ERROR, "optee::%s failed to remove file (FIDL status: %d, IO status: %d)\n",
               __FUNCTION__, status, io_status);
        message->set_return_code(TEEC_ERROR_GENERIC);
        return status;
    }

    message->set_return_code(TEEC_SUCCESS);
    return ZX_OK;
}

zx_status_t OpteeClient::HandleRpcCommandFileSystemRenameFile(
    RenameFileFileSystemRpcMessage* message) {
    ZX_DEBUG_ASSERT(message != nullptr);
    ZX_DEBUG_ASSERT(message->is_valid());

    zxlogf(SPEW, "optee: received RPC to rename file\n");

    std::optional<SharedMemoryView> old_path_mem = GetMemoryReference(
        FindSharedMemory(message->old_file_name_memory_identifier()),
        message->old_file_name_memory_paddr(),
        message->old_file_name_memory_size());
    if (!old_path_mem.has_value()) {
        message->set_return_code(TEEC_ERROR_BAD_PARAMETERS);
        return ZX_ERR_INVALID_ARGS;
    }

    std::filesystem::path old_path = GetPathFromRawMemory(
        reinterpret_cast<void*>(old_path_mem->vaddr()), message->old_file_name_memory_size());
    std::string old_name = old_path.filename().string();

    std::optional<SharedMemoryView> new_path_mem = GetMemoryReference(
        FindSharedMemory(message->new_file_name_memory_identifier()),
        message->new_file_name_memory_paddr(),
        message->new_file_name_memory_size());
    if (!new_path_mem.has_value()) {
        message->set_return_code(TEEC_ERROR_BAD_PARAMETERS);
        return ZX_ERR_INVALID_ARGS;
    }

    std::filesystem::path new_path = GetPathFromRawMemory(
        reinterpret_cast<void*>(new_path_mem->vaddr()), message->new_file_name_memory_size());
    std::string new_name = new_path.filename().string();

    zx::channel new_storage_channel;
    constexpr bool kNoCreate = false;
    zx_status_t status = GetStorageDirectory(new_path.parent_path(), kNoCreate,
                                             &new_storage_channel);
    if (status != ZX_OK) {
        message->set_return_code(TEEC_ERROR_BAD_STATE);
        return status;
    }

    if (!message->should_overwrite()) {
        zx::channel destination_channel;
        static constexpr uint32_t kCheckRenameFlags = fuchsia_io_OPEN_RIGHT_READABLE |
                                                      fuchsia_io_OPEN_FLAG_DESCRIBE;
        static constexpr uint32_t kCheckRenameMode = fuchsia_io_MODE_TYPE_FILE |
                                                     fuchsia_io_MODE_TYPE_DIRECTORY;
        status = OpenObjectInDirectory(new_storage_channel,
                                       kCheckRenameFlags,
                                       kCheckRenameMode,
                                       new_name,
                                       &destination_channel);
        if (status == ZX_OK) {
            // The file exists but shouldn't be overwritten
            zxlogf(INFO,
                   "optee::%s: refusing to rename file to path that already exists with overwrite "
                   "set to false\n",
                   __FUNCTION__);
            message->set_return_code(TEEC_ERROR_ACCESS_CONFLICT);
            return ZX_OK;
        } else if (status != ZX_ERR_NOT_FOUND) {
            zxlogf(ERROR, "optee::%s: could not check file existence before renaming (status %d)\n",
                   __FUNCTION__, status);
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

    zx_status_t io_status = ZX_OK;
    zx::handle new_storage_token;
    status = fuchsia_io_DirectoryGetToken(new_storage_channel.get(),
                                          &io_status,
                                          new_storage_token.reset_and_get_address());
    if (status != ZX_OK || io_status != ZX_OK) {
        zxlogf(ERROR,
               "optee::%s: could not get destination directory's storage token "
               "(FIDL status: %d, IO status: %d)\n",
               __FUNCTION__, status, io_status);
        message->set_return_code(TEEC_ERROR_GENERIC);
        return status;
    }

    status = fuchsia_io_DirectoryRename(old_storage_channel.get(),   // _channel
                                        old_name.data(),             // src_data
                                        old_name.length(),           // src_size
                                        new_storage_token.release(), // dst_parent_token
                                        new_name.data(),             // dst_data
                                        new_name.length(),           // dst_size
                                        &io_status);                 // out_s
    if (status != ZX_OK || io_status != ZX_OK) {
        zxlogf(ERROR, "optee::%s failed to rename file (FIDL status: %d, IO status: %d)\n",
               __FUNCTION__, status, io_status);
        message->set_return_code(TEEC_ERROR_GENERIC);
        return status;
    }

    message->set_return_code(TEEC_SUCCESS);
    return ZX_OK;
}

} // namespace optee
