// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentSyncServerTmpl = `
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
  fidl_message_header_t* hdr = reinterpret_cast<fidl_message_header_t*>(msg->bytes);
  switch (hdr->ordinal) {
  {{- range .Methods }}
    {{- if .HasRequest }}
    case {{ .OrdinalName }}:
    {
      auto result = ::fidl::DecodeAs<{{ .Name }}Request>(msg);
      if (result.status != ZX_OK) {
        txn->InternalError({::fidl::UnbindInfo::kDecodeError, result.status});
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
    txn->InternalError({::fidl::UnbindInfo::kUnexpectedMessage, ZX_ERR_NOT_SUPPORTED});
  }
  return found;
}
{{- end }}
`
