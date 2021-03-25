// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentSyncEventHandlerTmpl = `
{{- define "EventHandlerHandleOneEventMethodDefinition" }}
{{ EnsureNamespace . }}
::fidl::Result {{ .Name }}::SyncEventHandler::HandleOneEvent(
    ::fidl::UnownedClientEnd<{{ . }}> client_end) {
  zx_status_t status = client_end.channel()->wait_one(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
                                                      ::zx::time::infinite(),
                                                      nullptr);
  if (status != ZX_OK) {
    return ::fidl::Result(status, ::fidl::kErrorWaitOneFailed);
  }
  constexpr uint32_t kHandleAllocSize = ([]() constexpr {
    uint32_t x = 0;
    {{- range .Events }}
    if ({{ .WireResponse }}::MaxNumHandles >= x) {
      x = {{ .WireResponse }}::MaxNumHandles;
    }
    {{- end }}
    if (x > ZX_CHANNEL_MAX_MSG_HANDLES) {
      x = ZX_CHANNEL_MAX_MSG_HANDLES;
    }
    return x;
  })();
  {{ .SyncEventAllocation.ByteBufferType }} read_storage;
  uint8_t* read_bytes = read_storage.data();
  zx_handle_info_t read_handles[kHandleAllocSize];
  uint32_t actual_bytes;
  uint32_t actual_handles;
  status = client_end.channel()->read_etc(ZX_CHANNEL_READ_MAY_DISCARD,
                                          read_bytes, read_handles,
                                          read_storage.size(), kHandleAllocSize,
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
    case {{ .Protocol.Namespace }}::{{ .OrdinalName }}: {
      const char* error_message;
      zx_status_t status = fidl_decode_etc({{ .WireResponse }}::Type, read_bytes, actual_bytes,
                                           read_handles, actual_handles, &error_message);
      if (status != ZX_OK) {
        return ::fidl::Result(status, error_message);
      }
      {{ .Name }}(reinterpret_cast<{{ .WireResponse }}*>(read_bytes));
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
