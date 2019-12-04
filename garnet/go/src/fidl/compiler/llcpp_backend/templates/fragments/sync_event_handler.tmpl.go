// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fragments

const SyncEventHandler = `
{{- define "SyncEventHandlerIndividualMethodSignature" -}}
  {{- if .Response -}}
({{ template "Params" .Response }})
  {{- else -}}
()
  {{- end -}}
{{- end }}

{{- define "SyncEventHandlerMoveParams" }}
  {{- range $index, $param := . }}
    {{- if $index }}, {{ end -}} std::move(message->{{ $param.Name }})
  {{- end }}
{{- end }}

{{- define "SyncEventHandlerMethodDefinition" }}
zx_status_t {{ .Name }}::SyncClient::HandleEvents({{ .Name }}::EventHandlers handlers) {
  return {{ .Name }}::Call::HandleEvents(::zx::unowned_channel(channel_), std::move(handlers));
}
{{- end }}

{{- define "StaticCallSyncEventHandlerMethodDefinition" }}
zx_status_t {{ .Name }}::Call::HandleEvents(::zx::unowned_channel client_end, {{ .Name }}::EventHandlers handlers) {
  zx_status_t status = client_end->wait_one(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
                                            ::zx::time::infinite(),
                                            nullptr);
  if (status != ZX_OK) {
    return status;
  }
  constexpr uint32_t kReadAllocSize = ([]() constexpr {
    uint32_t x = 0;
    {{- range FilterMethodsWithReqs .Methods }}
    if (::fidl::internal::ClampedMessageSize<{{ .Name }}Response, ::fidl::MessageDirection::kReceiving>() >= x) {
      x = ::fidl::internal::ClampedMessageSize<{{ .Name }}Response, ::fidl::MessageDirection::kReceiving>();
    }
      {{- if .ResponseContainsUnion }}
    if (::fidl::internal::ClampedMessageSize<{{ .Name }}Response, ::fidl::MessageDirection::kReceiving, ::fidl::internal::WireFormatGuide::kAlternate>() >= x) {
      x = ::fidl::internal::ClampedMessageSize<{{ .Name }}Response, ::fidl::MessageDirection::kReceiving, ::fidl::internal::WireFormatGuide::kAlternate>();
    }
      {{- end }}
    {{- end }}
    return x;
  })();
  constexpr uint32_t kHandleAllocSize = ([]() constexpr {
    uint32_t x = 0;
    {{- range FilterMethodsWithReqs .Methods }}
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
  uint8_t* read_bytes = read_storage.buffer().data();
  zx_handle_t read_handles[kHandleAllocSize];
  uint32_t actual_bytes;
  uint32_t actual_handles;
  status = client_end->read(ZX_CHANNEL_READ_MAY_DISCARD,
                            read_bytes, read_handles,
                            kReadAllocSize, kHandleAllocSize,
                            &actual_bytes, &actual_handles);
  if (status == ZX_ERR_BUFFER_TOO_SMALL) {
    // Message size is unexpectedly larger than calculated.
    // This can only be due to a newer version of the protocol defining a new event,
    // whose size exceeds the maximum of known events in the current protocol.
    return handlers.unknown();
  }
  if (status != ZX_OK) {
    return status;
  }
  if (actual_bytes < sizeof(fidl_message_header_t)) {
    zx_handle_close_many(read_handles, actual_handles);
    return ZX_ERR_INVALID_ARGS;
  }
  auto msg = fidl_msg_t {
      .bytes = read_bytes,
      .handles = read_handles,
      .num_bytes = actual_bytes,
      .num_handles = actual_handles
  };
  fidl_message_header_t* hdr = reinterpret_cast<fidl_message_header_t*>(msg.bytes);
  status = fidl_validate_txn_header(hdr);
  if (status != ZX_OK) {
    return status;
  }
  switch (hdr->ordinal) {
  {{- range .Methods }}
    {{- if not .HasRequest }}
      {{- range .Ordinals.Reads }}
    case {{ .Name }}:
      {{- end }}
    {
      {{- if .ResponseContainsUnion }}
      constexpr uint32_t kTransformerDestSize = ::fidl::internal::ClampedMessageSize<{{ .Name }}Response, ::fidl::MessageDirection::kReceiving>();
      ::fidl::internal::ByteStorage<kTransformerDestSize> transformer_dest_storage(::fidl::internal::DelayAllocation);
      if (fidl_should_decode_union_from_xunion(hdr)) {
        transformer_dest_storage.Allocate();
        uint8_t* transformer_dest = transformer_dest_storage.buffer().data();
        zx_status_t transform_status = fidl_transform(FIDL_TRANSFORMATION_V1_TO_OLD,
                                                      {{ .Name }}Response::AltType,
                                                      reinterpret_cast<uint8_t*>(msg.bytes),
                                                      msg.num_bytes,
                                                      transformer_dest,
                                                      kTransformerDestSize,
                                                      &msg.num_bytes,
                                                      nullptr);
        if (transform_status != ZX_OK) {
          zx_handle_close_many(msg.handles, msg.num_handles);
          return ZX_ERR_INVALID_ARGS;
        }
        msg.bytes = transformer_dest;
      }
      {{- end }}
      auto result = ::fidl::DecodeAs<{{ .Name }}Response>(&msg);
      if (result.status != ZX_OK) {
        return result.status;
      }
      {{- if .Response }}
      auto message = result.message.message();
      {{- end }}
      return handlers.{{ .NameInLowerSnakeCase }}({{ template "SyncEventHandlerMoveParams" .Response }});
    }
    {{- end }}
  {{- end }}
    default:
      zx_handle_close_many(read_handles, actual_handles);
      return handlers.unknown();
  }
}
{{- end }}
`
