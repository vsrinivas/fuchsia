// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl.h"

#include <utility>

#include <fbl/algorithm.h>
#include <fbl/unique_ptr.h>
#include <lib/fidl/cpp/builder.h>
#include <lib/fidl/cpp/message.h>
#include <lib/fidl/cpp/message_part.h>

namespace mock_device {

namespace {

template <typename MessageType>
zx_status_t ParseActions(const fidl_type_t* type, fidl::Message* msg,
                         fbl::Array<const fuchsia_device_mock_Action>* actions_out) {
    const char* err_out = nullptr;
    zx_status_t status = msg->Decode(type, &err_out);
    if (status != ZX_OK) {
        printf("mock-device: Failed to decode: %s\n", err_out);
        return status;
    }
    auto payload = msg->GetBytesAs<MessageType>();
    auto array = std::make_unique<fuchsia_device_mock_Action[]>(payload->actions.count);
    memcpy(array.get(), payload->actions.data,
           payload->actions.count * sizeof(fuchsia_device_mock_Action));
    actions_out->reset(array.release(), payload->actions.count);
    return ZX_OK;
}

} // namespace

zx_status_t WaitForPerformActions(const zx::channel& c,
                                  fbl::Array<const fuchsia_device_mock_Action>* actions_out) {
    zx_signals_t signals;
    zx_status_t status = c.wait_one(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
                                    zx::time::infinite(), &signals);
    if (status != ZX_OK) {
        return status;
    }
    if (!(signals & ZX_CHANNEL_READABLE)) {
        return ZX_ERR_STOP;
    }

    FIDL_ALIGNDECL uint8_t request_buf[ZX_CHANNEL_MAX_MSG_BYTES];
    zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
    fidl::Message request(fidl::BytePart(request_buf, sizeof(request_buf)),
                          fidl::HandlePart(handles, fbl::count_of(handles)));
    status = request.Read(c.get(), 0);
    if (status != ZX_OK) {
        return status;
    }

    return ParseActions<fuchsia_device_mock_MockDeviceThreadPerformActionsRequest>(
            &fuchsia_device_mock_MockDeviceThreadPerformActionsRequestTable,
            &request, actions_out);
}

zx_status_t BindHook(const zx::channel& c, const fuchsia_device_mock_HookInvocation& record,
                     fbl::Array<const fuchsia_device_mock_Action>* actions_out) {
    using RequestType = fuchsia_device_mock_MockDeviceBindRequest;
    using ResponseType = fuchsia_device_mock_MockDeviceBindResponse;
    const auto& kRequestOrdinal = fuchsia_device_mock_MockDeviceBindOrdinal;
    const fidl_type_t* kResponseTable = &fuchsia_device_mock_MockDeviceBindResponseTable;

    FIDL_ALIGNDECL char wr_bytes[sizeof(RequestType)];
    fidl::Builder builder(wr_bytes, sizeof(wr_bytes));

    auto req = builder.New<RequestType>();
    ZX_ASSERT(req != nullptr);
    req->hdr.ordinal = kRequestOrdinal;
    req->hdr.txid = 0;
    req->record = record;

    fidl::Message msg(builder.Finalize(), fidl::HandlePart(nullptr, 0));
    FIDL_ALIGNDECL uint8_t response_buf[ZX_CHANNEL_MAX_MSG_BYTES];
    zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
    fidl::Message response(fidl::BytePart(response_buf, sizeof(response_buf)),
                           fidl::HandlePart(handles, fbl::count_of(handles)));
    zx_status_t status = msg.Call(c.get(), 0, ZX_TIME_INFINITE, &response);
    if (status != ZX_OK) {
        return status;
    }
    return ParseActions<ResponseType>(kResponseTable, &response, actions_out);
}

zx_status_t ReleaseHook(const zx::channel& c, const fuchsia_device_mock_HookInvocation& record) {
    using RequestType = fuchsia_device_mock_MockDeviceReleaseRequest;
    const auto& kRequestOrdinal = fuchsia_device_mock_MockDeviceReleaseOrdinal;

    FIDL_ALIGNDECL char wr_bytes[sizeof(RequestType)];
    fidl::Builder builder(wr_bytes, sizeof(wr_bytes));

    auto req = builder.New<RequestType>();
    ZX_ASSERT(req != nullptr);
    req->hdr.ordinal = kRequestOrdinal;
    req->hdr.txid = 0;
    req->record = record;

    fidl::Message msg(builder.Finalize(), fidl::HandlePart(nullptr, 0));
    return msg.Write(c.get(), 0);
}

zx_status_t GetProtocolHook(const zx::channel& c, const fuchsia_device_mock_HookInvocation& record,
                            uint32_t protocol_id,
                            fbl::Array<const fuchsia_device_mock_Action>* actions_out) {
    using RequestType = fuchsia_device_mock_MockDeviceGetProtocolRequest;
    using ResponseType = fuchsia_device_mock_MockDeviceGetProtocolResponse;
    const auto& kRequestOrdinal = fuchsia_device_mock_MockDeviceGetProtocolOrdinal;
    const fidl_type_t* kResponseTable = &fuchsia_device_mock_MockDeviceGetProtocolResponseTable;

    FIDL_ALIGNDECL char wr_bytes[sizeof(RequestType)];
    fidl::Builder builder(wr_bytes, sizeof(wr_bytes));

    auto req = builder.New<RequestType>();
    ZX_ASSERT(req != nullptr);
    req->hdr.ordinal = kRequestOrdinal;
    req->hdr.txid = 0;
    req->record = record;
    req->protocol_id = protocol_id;

    fidl::Message msg(builder.Finalize(), fidl::HandlePart(nullptr, 0));
    FIDL_ALIGNDECL uint8_t response_buf[ZX_CHANNEL_MAX_MSG_BYTES];
    zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
    fidl::Message response(fidl::BytePart(response_buf, sizeof(response_buf)),
                           fidl::HandlePart(handles, fbl::count_of(handles)));
    zx_status_t status = msg.Call(c.get(), 0, ZX_TIME_INFINITE, &response);
    if (status != ZX_OK) {
        return status;
    }
    return ParseActions<ResponseType>(kResponseTable, &response, actions_out);
}

zx_status_t OpenHook(const zx::channel& c, const fuchsia_device_mock_HookInvocation& record,
                     uint32_t flags,
                     fbl::Array<const fuchsia_device_mock_Action>* actions_out) {
    using RequestType = fuchsia_device_mock_MockDeviceOpenRequest;
    using ResponseType = fuchsia_device_mock_MockDeviceOpenResponse;
    const auto& kRequestOrdinal = fuchsia_device_mock_MockDeviceOpenOrdinal;
    const fidl_type_t* kResponseTable = &fuchsia_device_mock_MockDeviceOpenResponseTable;

    FIDL_ALIGNDECL char wr_bytes[sizeof(RequestType)];
    fidl::Builder builder(wr_bytes, sizeof(wr_bytes));

    auto req = builder.New<RequestType>();
    ZX_ASSERT(req != nullptr);
    req->hdr.ordinal = kRequestOrdinal;
    req->hdr.txid = 0;
    req->record = record;
    req->flags = flags;

    fidl::Message msg(builder.Finalize(), fidl::HandlePart(nullptr, 0));
    FIDL_ALIGNDECL uint8_t response_buf[ZX_CHANNEL_MAX_MSG_BYTES];
    zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
    fidl::Message response(fidl::BytePart(response_buf, sizeof(response_buf)),
                           fidl::HandlePart(handles, fbl::count_of(handles)));
    zx_status_t status = msg.Call(c.get(), 0, ZX_TIME_INFINITE, &response);
    if (status != ZX_OK) {
        return status;
    }
    return ParseActions<ResponseType>(kResponseTable, &response, actions_out);
}

zx_status_t OpenAtHook(const zx::channel& c, const fuchsia_device_mock_HookInvocation& record,
                       const fbl::StringPiece& path, uint32_t flags,
                       fbl::Array<const fuchsia_device_mock_Action>* actions_out) {
    using RequestType = fuchsia_device_mock_MockDeviceOpenAtRequest;
    using ResponseType = fuchsia_device_mock_MockDeviceOpenAtResponse;
    const auto& kRequestOrdinal = fuchsia_device_mock_MockDeviceOpenAtOrdinal;
    const fidl_type_t* kResponseTable = &fuchsia_device_mock_MockDeviceOpenAtResponseTable;

    FIDL_ALIGNDECL char wr_bytes[sizeof(RequestType)];
    fidl::Builder builder(wr_bytes, sizeof(wr_bytes));

    auto req = builder.New<RequestType>();
    ZX_ASSERT(req != nullptr);
    req->hdr.ordinal = kRequestOrdinal;
    req->hdr.txid = 0;
    req->record = record;
    req->flags = flags;

    req->path.data = reinterpret_cast<char*>(FIDL_ALLOC_PRESENT);
    req->path.size = path.size();
    uint8_t* path_storage = builder.NewArray<uint8_t>(static_cast<uint32_t>(path.size()));
    memcpy(path_storage, path.data(), path.size());

    fidl::Message msg(builder.Finalize(), fidl::HandlePart(nullptr, 0));
    FIDL_ALIGNDECL uint8_t response_buf[ZX_CHANNEL_MAX_MSG_BYTES];
    zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
    fidl::Message response(fidl::BytePart(response_buf, sizeof(response_buf)),
                           fidl::HandlePart(handles, fbl::count_of(handles)));
    zx_status_t status = msg.Call(c.get(), 0, ZX_TIME_INFINITE, &response);
    if (status != ZX_OK) {
        return status;
    }
    return ParseActions<ResponseType>(kResponseTable, &response, actions_out);
}

zx_status_t CloseHook(const zx::channel& c, const fuchsia_device_mock_HookInvocation& record,
                      uint32_t flags, fbl::Array<const fuchsia_device_mock_Action>* actions_out) {
    using RequestType = fuchsia_device_mock_MockDeviceCloseRequest;
    using ResponseType = fuchsia_device_mock_MockDeviceCloseResponse;
    const auto& kRequestOrdinal = fuchsia_device_mock_MockDeviceCloseOrdinal;
    const fidl_type_t* kResponseTable = &fuchsia_device_mock_MockDeviceCloseResponseTable;

    FIDL_ALIGNDECL char wr_bytes[sizeof(RequestType)];
    fidl::Builder builder(wr_bytes, sizeof(wr_bytes));

    auto req = builder.New<RequestType>();
    ZX_ASSERT(req != nullptr);
    req->hdr.ordinal = kRequestOrdinal;
    req->hdr.txid = 0;
    req->record = record;
    req->flags = flags;

    fidl::Message msg(builder.Finalize(), fidl::HandlePart(nullptr, 0));
    FIDL_ALIGNDECL uint8_t response_buf[ZX_CHANNEL_MAX_MSG_BYTES];
    zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
    fidl::Message response(fidl::BytePart(response_buf, sizeof(response_buf)),
                           fidl::HandlePart(handles, fbl::count_of(handles)));
    zx_status_t status = msg.Call(c.get(), 0, ZX_TIME_INFINITE, &response);
    if (status != ZX_OK) {
        return status;
    }
    return ParseActions<ResponseType>(kResponseTable, &response, actions_out);
}

zx_status_t UnbindHook(const zx::channel& c, const fuchsia_device_mock_HookInvocation& record,
                       fbl::Array<const fuchsia_device_mock_Action>* actions_out) {
    using RequestType = fuchsia_device_mock_MockDeviceUnbindRequest;
    using ResponseType = fuchsia_device_mock_MockDeviceUnbindResponse;
    const auto& kRequestOrdinal = fuchsia_device_mock_MockDeviceUnbindOrdinal;
    const fidl_type_t* kResponseTable = &fuchsia_device_mock_MockDeviceUnbindResponseTable;

    FIDL_ALIGNDECL char wr_bytes[sizeof(RequestType)];
    fidl::Builder builder(wr_bytes, sizeof(wr_bytes));

    auto req = builder.New<RequestType>();
    ZX_ASSERT(req != nullptr);
    req->hdr.ordinal = kRequestOrdinal;
    req->hdr.txid = 0;
    req->record = record;

    fidl::Message msg(builder.Finalize(), fidl::HandlePart(nullptr, 0));
    FIDL_ALIGNDECL uint8_t response_buf[ZX_CHANNEL_MAX_MSG_BYTES];
    zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
    fidl::Message response(fidl::BytePart(response_buf, sizeof(response_buf)),
                           fidl::HandlePart(handles, fbl::count_of(handles)));
    zx_status_t status = msg.Call(c.get(), 0, ZX_TIME_INFINITE, &response);
    if (status != ZX_OK) {
        return status;
    }
    return ParseActions<ResponseType>(kResponseTable, &response, actions_out);
}

zx_status_t ReadHook(const zx::channel& c, const fuchsia_device_mock_HookInvocation& record,
                     uint64_t count, zx_off_t off,
                     fbl::Array<const fuchsia_device_mock_Action>* actions_out) {
    using RequestType = fuchsia_device_mock_MockDeviceReadRequest;
    using ResponseType = fuchsia_device_mock_MockDeviceReadResponse;
    const auto& kRequestOrdinal = fuchsia_device_mock_MockDeviceReadOrdinal;
    const fidl_type_t* kResponseTable = &fuchsia_device_mock_MockDeviceReadResponseTable;

    FIDL_ALIGNDECL char wr_bytes[sizeof(RequestType)];
    fidl::Builder builder(wr_bytes, sizeof(wr_bytes));

    auto req = builder.New<RequestType>();
    ZX_ASSERT(req != nullptr);
    req->hdr.ordinal = kRequestOrdinal;
    req->hdr.txid = 0;
    req->count = count;
    req->off = off;

    fidl::Message msg(builder.Finalize(), fidl::HandlePart(nullptr, 0));
    FIDL_ALIGNDECL uint8_t response_buf[ZX_CHANNEL_MAX_MSG_BYTES];
    zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
    fidl::Message response(fidl::BytePart(response_buf, sizeof(response_buf)),
                           fidl::HandlePart(handles, fbl::count_of(handles)));
    zx_status_t status = msg.Call(c.get(), 0, ZX_TIME_INFINITE, &response);
    if (status != ZX_OK) {
        return status;
    }
    return ParseActions<ResponseType>(kResponseTable, &response, actions_out);
}

zx_status_t GetSizeHook(const zx::channel& c, const fuchsia_device_mock_HookInvocation& record,
                        fbl::Array<const fuchsia_device_mock_Action>* actions_out) {
    using RequestType = fuchsia_device_mock_MockDeviceGetSizeRequest;
    using ResponseType = fuchsia_device_mock_MockDeviceGetSizeResponse;
    const auto& kRequestOrdinal = fuchsia_device_mock_MockDeviceGetSizeOrdinal;
    const fidl_type_t* kResponseTable = &fuchsia_device_mock_MockDeviceGetSizeResponseTable;

    FIDL_ALIGNDECL char wr_bytes[sizeof(RequestType)];
    fidl::Builder builder(wr_bytes, sizeof(wr_bytes));

    auto req = builder.New<RequestType>();
    ZX_ASSERT(req != nullptr);
    req->hdr.ordinal = kRequestOrdinal;
    req->hdr.txid = 0;

    fidl::Message msg(builder.Finalize(), fidl::HandlePart(nullptr, 0));
    FIDL_ALIGNDECL uint8_t response_buf[ZX_CHANNEL_MAX_MSG_BYTES];
    zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
    fidl::Message response(fidl::BytePart(response_buf, sizeof(response_buf)),
                           fidl::HandlePart(handles, fbl::count_of(handles)));
    zx_status_t status = msg.Call(c.get(), 0, ZX_TIME_INFINITE, &response);
    if (status != ZX_OK) {
        return status;
    }
    return ParseActions<ResponseType>(kResponseTable, &response, actions_out);
}

zx_status_t SuspendHook(const zx::channel& c, const fuchsia_device_mock_HookInvocation& record,
                        uint32_t flags, fbl::Array<const fuchsia_device_mock_Action>* actions_out) {
    using RequestType = fuchsia_device_mock_MockDeviceSuspendRequest;
    using ResponseType = fuchsia_device_mock_MockDeviceSuspendResponse;
    const auto& kRequestOrdinal = fuchsia_device_mock_MockDeviceSuspendOrdinal;
    const fidl_type_t* kResponseTable = &fuchsia_device_mock_MockDeviceSuspendResponseTable;

    FIDL_ALIGNDECL char wr_bytes[sizeof(RequestType)];
    fidl::Builder builder(wr_bytes, sizeof(wr_bytes));

    auto req = builder.New<RequestType>();
    ZX_ASSERT(req != nullptr);
    req->hdr.ordinal = kRequestOrdinal;
    req->hdr.txid = 0;
    req->record = record;
    req->flags = flags;

    fidl::Message msg(builder.Finalize(), fidl::HandlePart(nullptr, 0));
    FIDL_ALIGNDECL uint8_t response_buf[ZX_CHANNEL_MAX_MSG_BYTES];
    zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
    fidl::Message response(fidl::BytePart(response_buf, sizeof(response_buf)),
                           fidl::HandlePart(handles, fbl::count_of(handles)));
    zx_status_t status = msg.Call(c.get(), 0, ZX_TIME_INFINITE, &response);
    if (status != ZX_OK) {
        return status;
    }
    return ParseActions<ResponseType>(kResponseTable, &response, actions_out);
}

zx_status_t ResumeHook(const zx::channel& c, const fuchsia_device_mock_HookInvocation& record,
                       uint32_t flags, fbl::Array<const fuchsia_device_mock_Action>* actions_out) {
    using RequestType = fuchsia_device_mock_MockDeviceResumeRequest;
    using ResponseType = fuchsia_device_mock_MockDeviceResumeResponse;
    const auto& kRequestOrdinal = fuchsia_device_mock_MockDeviceResumeOrdinal;
    const fidl_type_t* kResponseTable = &fuchsia_device_mock_MockDeviceResumeResponseTable;

    FIDL_ALIGNDECL char wr_bytes[sizeof(RequestType)];
    fidl::Builder builder(wr_bytes, sizeof(wr_bytes));

    auto req = builder.New<RequestType>();
    ZX_ASSERT(req != nullptr);
    req->hdr.ordinal = kRequestOrdinal;
    req->hdr.txid = 0;
    req->record = record;
    req->flags = flags;

    fidl::Message msg(builder.Finalize(), fidl::HandlePart(nullptr, 0));
    FIDL_ALIGNDECL uint8_t response_buf[ZX_CHANNEL_MAX_MSG_BYTES];
    zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
    fidl::Message response(fidl::BytePart(response_buf, sizeof(response_buf)),
                           fidl::HandlePart(handles, fbl::count_of(handles)));
    zx_status_t status = msg.Call(c.get(), 0, ZX_TIME_INFINITE, &response);
    if (status != ZX_OK) {
        return status;
    }
    return ParseActions<ResponseType>(kResponseTable, &response, actions_out);
}

zx_status_t WriteHook(const zx::channel& c, const fuchsia_device_mock_HookInvocation& record,
                      const uint8_t* buffer_data, size_t buffer_count, zx_off_t off,
                      fbl::Array<const fuchsia_device_mock_Action>* actions_out) {
    using RequestType = fuchsia_device_mock_MockDeviceWriteRequest;
    using ResponseType = fuchsia_device_mock_MockDeviceWriteResponse;
    const auto& kRequestOrdinal = fuchsia_device_mock_MockDeviceWriteOrdinal;
    const fidl_type_t* kResponseTable = &fuchsia_device_mock_MockDeviceWriteResponseTable;

    FIDL_ALIGNDECL char wr_bytes[sizeof(RequestType)];
    fidl::Builder builder(wr_bytes, sizeof(wr_bytes));

    auto req = builder.New<RequestType>();
    ZX_ASSERT(req != nullptr);
    req->hdr.ordinal = kRequestOrdinal;
    req->hdr.txid = 0;
    req->off = off;

    req->buffer.data = reinterpret_cast<char*>(FIDL_ALLOC_PRESENT);
    req->buffer.count = buffer_count;
    uint8_t* buffer_storage = builder.NewArray<uint8_t>(static_cast<uint32_t>(buffer_count));
    memcpy(buffer_storage, buffer_data, buffer_count);

    fidl::Message msg(builder.Finalize(), fidl::HandlePart(nullptr, 0));
    FIDL_ALIGNDECL uint8_t response_buf[ZX_CHANNEL_MAX_MSG_BYTES];
    zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
    fidl::Message response(fidl::BytePart(response_buf, sizeof(response_buf)),
                           fidl::HandlePart(handles, fbl::count_of(handles)));
    zx_status_t status = msg.Call(c.get(), 0, ZX_TIME_INFINITE, &response);
    if (status != ZX_OK) {
        return status;
    }
    return ParseActions<ResponseType>(kResponseTable, &response, actions_out);
}

zx_status_t IoctlHook(const zx::channel& c, const fuchsia_device_mock_HookInvocation& record,
                      uint32_t op, const uint8_t* in_data, size_t in_count, uint64_t out_count,
                      fbl::Array<const fuchsia_device_mock_Action>* actions_out) {
    using RequestType = fuchsia_device_mock_MockDeviceIoctlRequest;
    using ResponseType = fuchsia_device_mock_MockDeviceIoctlResponse;
    const auto& kRequestOrdinal = fuchsia_device_mock_MockDeviceIoctlOrdinal;
    const fidl_type_t* kResponseTable = &fuchsia_device_mock_MockDeviceIoctlResponseTable;

    FIDL_ALIGNDECL char wr_bytes[sizeof(RequestType)];
    fidl::Builder builder(wr_bytes, sizeof(wr_bytes));

    auto req = builder.New<RequestType>();
    ZX_ASSERT(req != nullptr);
    req->hdr.ordinal = kRequestOrdinal;
    req->hdr.txid = 0;
    req->op = op;
    req->out_count = out_count;

    req->in.data = reinterpret_cast<char*>(FIDL_ALLOC_PRESENT);
    req->in.count = in_count;
    uint8_t* in_storage = builder.NewArray<uint8_t>(static_cast<uint32_t>(in_count));
    memcpy(in_storage, in_data, in_count);

    fidl::Message msg(builder.Finalize(), fidl::HandlePart(nullptr, 0));
    FIDL_ALIGNDECL uint8_t response_buf[ZX_CHANNEL_MAX_MSG_BYTES];
    zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
    fidl::Message response(fidl::BytePart(response_buf, sizeof(response_buf)),
                           fidl::HandlePart(handles, fbl::count_of(handles)));
    zx_status_t status = msg.Call(c.get(), 0, ZX_TIME_INFINITE, &response);
    if (status != ZX_OK) {
        return status;
    }
    return ParseActions<ResponseType>(kResponseTable, &response, actions_out);
}

zx_status_t MessageHook(const zx::channel& c, const fuchsia_device_mock_HookInvocation& record,
                        fbl::Array<const fuchsia_device_mock_Action>* actions_out) {
    using RequestType = fuchsia_device_mock_MockDeviceMessageRequest;
    using ResponseType = fuchsia_device_mock_MockDeviceMessageResponse;
    const auto& kRequestOrdinal = fuchsia_device_mock_MockDeviceMessageOrdinal;
    const fidl_type_t* kResponseTable = &fuchsia_device_mock_MockDeviceMessageResponseTable;

    FIDL_ALIGNDECL char wr_bytes[sizeof(RequestType)];
    fidl::Builder builder(wr_bytes, sizeof(wr_bytes));

    auto req = builder.New<RequestType>();
    ZX_ASSERT(req != nullptr);
    req->hdr.ordinal = kRequestOrdinal;
    req->hdr.txid = 0;
    req->record = record;

    fidl::Message msg(builder.Finalize(), fidl::HandlePart(nullptr, 0));
    FIDL_ALIGNDECL uint8_t response_buf[ZX_CHANNEL_MAX_MSG_BYTES];
    zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
    fidl::Message response(fidl::BytePart(response_buf, sizeof(response_buf)),
                           fidl::HandlePart(handles, fbl::count_of(handles)));
    zx_status_t status = msg.Call(c.get(), 0, ZX_TIME_INFINITE, &response);
    if (status != ZX_OK) {
        return status;
    }
    return ParseActions<ResponseType>(kResponseTable, &response, actions_out);
}

zx_status_t RxrpcHook(const zx::channel& c, const fuchsia_device_mock_HookInvocation& record,
                      fbl::Array<const fuchsia_device_mock_Action>* actions_out) {
    using RequestType = fuchsia_device_mock_MockDeviceRxrpcRequest;
    using ResponseType = fuchsia_device_mock_MockDeviceRxrpcResponse;
    const auto& kRequestOrdinal = fuchsia_device_mock_MockDeviceRxrpcOrdinal;
    const fidl_type_t* kResponseTable = &fuchsia_device_mock_MockDeviceRxrpcResponseTable;

    FIDL_ALIGNDECL char wr_bytes[sizeof(RequestType)];
    fidl::Builder builder(wr_bytes, sizeof(wr_bytes));

    auto req = builder.New<RequestType>();
    ZX_ASSERT(req != nullptr);
    req->hdr.ordinal = kRequestOrdinal;
    req->hdr.txid = 0;
    req->record = record;

    fidl::Message msg(builder.Finalize(), fidl::HandlePart(nullptr, 0));
    FIDL_ALIGNDECL uint8_t response_buf[ZX_CHANNEL_MAX_MSG_BYTES];
    zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
    fidl::Message response(fidl::BytePart(response_buf, sizeof(response_buf)),
                           fidl::HandlePart(handles, fbl::count_of(handles)));
    zx_status_t status = msg.Call(c.get(), 0, ZX_TIME_INFINITE, &response);
    if (status != ZX_OK) {
        return status;
    }
    return ParseActions<ResponseType>(kResponseTable, &response, actions_out);
}

zx_status_t SendAddDeviceDone(const zx::channel& c, uint64_t action_id) {
    using RequestType = fuchsia_device_mock_MockDeviceAddDeviceDoneRequest;
    const auto& kRequestOrdinal = fuchsia_device_mock_MockDeviceAddDeviceDoneOrdinal;

    FIDL_ALIGNDECL char wr_bytes[sizeof(RequestType)];
    fidl::Builder builder(wr_bytes, sizeof(wr_bytes));

    auto req = builder.New<RequestType>();
    ZX_ASSERT(req != nullptr);
    req->hdr.ordinal = kRequestOrdinal;
    req->hdr.txid = 0;
    req->action_id = action_id;

    fidl::Message msg(builder.Finalize(), fidl::HandlePart(nullptr, 0));
    return msg.Write(c.get(), 0);
}

zx_status_t SendRemoveDeviceDone(const zx::channel& c, uint64_t action_id) {
    using RequestType = fuchsia_device_mock_MockDeviceRemoveDeviceDoneRequest;
    const auto& kRequestOrdinal = fuchsia_device_mock_MockDeviceRemoveDeviceDoneOrdinal;

    FIDL_ALIGNDECL char wr_bytes[sizeof(RequestType)];
    fidl::Builder builder(wr_bytes, sizeof(wr_bytes));

    auto req = builder.New<RequestType>();
    ZX_ASSERT(req != nullptr);
    req->hdr.ordinal = kRequestOrdinal;
    req->hdr.txid = 0;
    req->action_id = action_id;

    fidl::Message msg(builder.Finalize(), fidl::HandlePart(nullptr, 0));
    return msg.Write(c.get(), 0);
}

zx_status_t SendAddDeviceDoneFromThread(const zx::channel& c, uint64_t action_id) {
    using EventType = fuchsia_device_mock_MockDeviceThreadAddDeviceDoneEvent;
    const auto& kEventOrdinal = fuchsia_device_mock_MockDeviceThreadAddDeviceDoneOrdinal;

    FIDL_ALIGNDECL char wr_bytes[sizeof(EventType)];
    fidl::Builder builder(wr_bytes, sizeof(wr_bytes));

    auto req = builder.New<EventType>();
    ZX_ASSERT(req != nullptr);
    req->hdr.ordinal = kEventOrdinal;
    req->hdr.txid = 0;
    req->action_id = action_id;

    fidl::Message msg(builder.Finalize(), fidl::HandlePart(nullptr, 0));
    return msg.Write(c.get(), 0);
}

zx_status_t SendRemoveDeviceDoneFromThread(const zx::channel& c, uint64_t action_id) {
    using EventType = fuchsia_device_mock_MockDeviceThreadRemoveDeviceDoneEvent;
    const auto& kEventOrdinal = fuchsia_device_mock_MockDeviceThreadRemoveDeviceDoneOrdinal;

    FIDL_ALIGNDECL char wr_bytes[sizeof(EventType)];
    fidl::Builder builder(wr_bytes, sizeof(wr_bytes));

    auto req = builder.New<EventType>();
    ZX_ASSERT(req != nullptr);
    req->hdr.ordinal = kEventOrdinal;
    req->hdr.txid = 0;
    req->action_id = action_id;

    fidl::Message msg(builder.Finalize(), fidl::HandlePart(nullptr, 0));
    return msg.Write(c.get(), 0);
}

} // namespace mock_device
