// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentSyncEventHandlerTmpl = `
{{- define "EventHandlerHandleOneEventMethodDefinition" }}
::fidl::Result {{ .Name }}::SyncEventHandler::HandleOneEvent(::zx::unowned_channel client_end) {
  zx_status_t status = client_end->wait_one(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
                                            ::zx::time::infinite(),
                                            nullptr);
  if (status != ZX_OK) {
    return ::fidl::Result(status, ::fidl::kErrorWaitOneFailed);
  }
  constexpr uint32_t kReadAllocSize = ([]() constexpr {
    uint32_t x = 0;
    {{- range .Events }}
    if (::fidl::internal::ClampedMessageSize<{{ .Name }}Response, ::fidl::MessageDirection::kReceiving>() >= x) {
      x = ::fidl::internal::ClampedMessageSize<{{ .Name }}Response, ::fidl::MessageDirection::kReceiving>();
    }
    {{- end }}
    return x;
  })();
  constexpr uint32_t kHandleAllocSize = ([]() constexpr {
    uint32_t x = 0;
    {{- range .Events }}
    if ({{ .Name }}Response::MaxNumHandles >= x) {
      x = {{ .Name }}Response::MaxNumHandles;
    }
    {{- end }}
    if (x > ZX_CHANNEL_MAX_MSG_HANDLES) {
      x = ZX_CHANNEL_MAX_MSG_HANDLES;
    }
    return x;
  })();
  ::fidl::internal::ByteStorage<kReadAllocSize> read_storage;
  uint8_t* read_bytes = read_storage.data();
  zx_handle_info_t read_handles[kHandleAllocSize];
  uint32_t actual_bytes;
  uint32_t actual_handles;
  status = client_end->read_etc(ZX_CHANNEL_READ_MAY_DISCARD,
                                read_bytes, read_handles,
                                kReadAllocSize, kHandleAllocSize,
                                &actual_bytes, &actual_handles);
  if (status == ZX_ERR_BUFFER_TOO_SMALL) {
    // Message size is unexpectedly larger than calculated.
    // This can only be due to a newer version of the protocol defining a new event,
    // whose size exceeds the maximum of known events in the current protocol.
    return ::fidl::Result(Unknown(), nullptr);
  }
  if (status != ZX_OK) {
    return ::fidl::Result(status, ::fidl::kErrorReadFailed);
  }
  if (actual_bytes < sizeof(fidl_message_header_t)) {
    FidlHandleInfoCloseMany(read_handles, actual_handles);
    return ::fidl::Result(ZX_ERR_INVALID_ARGS, ::fidl::kErrorInvalidHeader);
  }
  fidl_message_header_t* hdr = reinterpret_cast<fidl_message_header_t*>(read_bytes);
  status = fidl_validate_txn_header(hdr);
  if (status != ZX_OK) {
    FidlHandleInfoCloseMany(read_handles, actual_handles);
    return ::fidl::Result(status, ::fidl::kErrorInvalidHeader);
  }
  switch (hdr->ordinal) {
  {{- range .Methods }}
    {{- if not .HasRequest }}
    case {{ .OrdinalName }}: {
      const char* error_message;
      zx_status_t status = fidl_decode_etc({{ .Name }}Response::Type, read_bytes, actual_bytes,
                                           read_handles, actual_handles, &error_message);
      if (status != ZX_OK) {
        return ::fidl::Result(status, error_message);
      }
      {{ .Name }}(reinterpret_cast<{{ .Name }}Response*>(read_bytes));
      return ::fidl::Result(ZX_OK, nullptr);
    }
    {{- end }}
  {{- end }}
    default: {
      FidlHandleInfoCloseMany(read_handles, actual_handles);
      return ::fidl::Result(Unknown(), nullptr);
    }
  }
}
{{- end }}
`
