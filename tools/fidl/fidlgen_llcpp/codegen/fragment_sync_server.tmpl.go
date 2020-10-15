// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentSyncServerTmpl = `
{{- define "SyncServerDispatchMethodSignature" -}}
(Interface* impl, fidl_incoming_msg_t* msg, ::fidl::Transaction* txn)
{{- end }}

{{- define "SyncServerDispatchMoveParams" }}
  {{- range $index, $param := . }}
    {{- if $index }}, {{ end -}} std::move(message->{{ $param.Name }})
  {{- end }}
{{- end }}

{{- define "SyncServerTryDispatchMethodDefinition" }}
namespace methods {
{{- range .Methods }}
  {{- if .HasRequest }}

void {{ .LLProps.ProtocolName }}Dispatch{{ .Name }}(void* interface, void* bytes,
    ::fidl::Transaction* txn) {
  {{- if .Request }}
  auto message = reinterpret_cast<{{ .LLProps.ProtocolName }}::{{ .Name }}Request*>(bytes);
  {{- end }}
  {{ .LLProps.ProtocolName }}::Interface::{{ .Name }}Completer::Sync completer(txn);
  reinterpret_cast<{{ .LLProps.ProtocolName }}::Interface*>(interface)
      ->{{ .Name }}({{ template "SyncServerDispatchMoveParams" .Request }}{{ if .Request }},{{ end }}
                    completer);
}
  {{- end }}
{{- end }}

}  // namespace methods

namespace entries {

::fidl::internal::MethodEntry {{ .Name }}[] = {
{{- range .Methods }}
  {{- if .HasRequest }}
  { {{ .OrdinalName }}, {{ .LLProps.ProtocolName }}::{{ .Name}}Request::Type,
    methods::{{ .LLProps.ProtocolName }}Dispatch{{ .Name }} },
  {{- end }}
{{- end }}
};

}  // namespace entries

::fidl::DispatchResult {{ .Name }}::TryDispatch{{ template "SyncServerDispatchMethodSignature" }} {
  {{- if HasMethodWithReqs .Methods }}
  return ::fidl::internal::TryDispatch(
      impl, msg, txn,
      entries::{{ .Name }},
      entries::{{ .Name }} + sizeof(entries::{{ .Name }}) / sizeof(::fidl::internal::MethodEntry));
  {{- else }}
    return ::fidl::DispatchResult::kNotFound;
  {{- end }}
}
{{- end }}

{{- define "SyncServerDispatchMethodDefinition" }}
::fidl::DispatchResult {{ .Name }}::Dispatch{{ template "SyncServerDispatchMethodSignature" }} {
  {{- if HasMethodWithReqs .Methods }}
  ::fidl::DispatchResult dispatch_result = TryDispatch(impl, msg, txn);
  if (dispatch_result == ::fidl::DispatchResult::kNotFound) {
    zx_handle_close_many(msg->handles, msg->num_handles);
    txn->InternalError({::fidl::UnbindInfo::kUnexpectedMessage, ZX_ERR_NOT_SUPPORTED});
  }
  return dispatch_result;
  {{- else }}
  zx_handle_close_many(msg->handles, msg->num_handles);
  txn->InternalError({::fidl::UnbindInfo::kUnexpectedMessage, ZX_ERR_NOT_SUPPORTED});
  return ::fidl::DispatchResult::kNotFound;
  {{- end }}
}
{{- end }}
`
