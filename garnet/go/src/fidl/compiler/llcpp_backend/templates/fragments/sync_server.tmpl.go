// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fragments

const SyncServer = `
{{- define "SyncServerDispatchMethodSignature" -}}
(Interface* impl, fidl_msg_t* msg, ::fidl::Transaction* txn)
{{- end }}

{{- define "SyncServerDispatchMoveParams" }}
  {{- range $index, $param := . }}
    {{- if $index }}, {{ end -}} std::move(message->{{ $param.Name }})
  {{- end }}
{{- end }}

{{- define "SyncServerTryDispatchMethodDefinition" }}
bool {{ .Name }}::TryDispatch{{ template "SyncServerDispatchMethodSignature" }} {
  if (msg->num_bytes < sizeof(fidl_message_header_t)) {
    zx_handle_close_many(msg->handles, msg->num_handles);
    txn->Close(ZX_ERR_INVALID_ARGS);
    return true;
  }
  fidl_message_header_t* hdr = reinterpret_cast<fidl_message_header_t*>(msg->bytes);
  zx_status_t status = fidl_validate_txn_header(hdr);
  if (status != ZX_OK) {
    txn->Close(status);
    return true;
  }
  switch (hdr->ordinal) {
  {{- range .Methods }}
    {{- if .HasRequest }}
      {{- range .Ordinals.Reads }}
    case {{ .Name }}:
      {{- end }}
    {
      {{- if .RequestContainsUnion }}
      constexpr uint32_t kTransformerDestSize = ::fidl::internal::ClampedMessageSize<{{ .Name }}Request, ::fidl::MessageDirection::kReceiving>();
      ::fidl::internal::ByteStorage<kTransformerDestSize> transformer_dest_storage(::fidl::internal::DelayAllocation);
      if (!fidl_should_decode_union_from_xunion(hdr)) {
        transformer_dest_storage.Allocate();
        uint8_t* transformer_dest = transformer_dest_storage.buffer().data();
        zx_status_t transform_status = fidl_transform(FIDL_TRANSFORMATION_OLD_TO_V1,
                                                      {{ .Name }}Request::AltType,
                                                      reinterpret_cast<uint8_t*>(msg->bytes),
                                                      msg->num_bytes,
                                                      transformer_dest,
                                                      kTransformerDestSize,
                                                      &msg->num_bytes,
                                                      nullptr);
        if (transform_status != ZX_OK) {
          txn->Close(ZX_ERR_INVALID_ARGS);
          zx_handle_close_many(msg->handles, msg->num_handles);
          return true;
        }
        msg->bytes = transformer_dest;
      }
      {{- end }}
      auto result = ::fidl::DecodeAs<{{ .Name }}Request>(msg);
      if (result.status != ZX_OK) {
        txn->Close(ZX_ERR_INVALID_ARGS);
        return true;
      }
      {{- if .Request }}
      auto message = result.message.message();
      {{- end }}
      impl->{{ .Name }}({{ template "SyncServerDispatchMoveParams" .Request }}{{ if .Request }},{{ end }}
          Interface::{{ .Name }}Completer::Sync(txn));
      return true;
    }
    {{- end }}
  {{- end }}
    default: {
      return false;
    }
  }
}
{{- end }}

{{- define "SyncServerDispatchMethodDefinition" }}
bool {{ .Name }}::Dispatch{{ template "SyncServerDispatchMethodSignature" }} {
  bool found = TryDispatch(impl, msg, txn);
  if (!found) {
    zx_handle_close_many(msg->handles, msg->num_handles);
    txn->Close(ZX_ERR_NOT_SUPPORTED);
  }
  return found;
}
{{- end }}
`
