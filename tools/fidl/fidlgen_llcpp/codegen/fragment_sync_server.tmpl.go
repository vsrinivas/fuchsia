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
namespace methods {
{{- range .Methods }}
  {{- if .HasRequest }}

void {{ .LLProps.ProtocolName }}Dispatch{{ .Name }}({{ .LLProps.ProtocolName }}::Interface* interface, void* bytes,
                         ::fidl::Transaction* txn) {
  {{- if .Request }}
  auto message = reinterpret_cast<{{ .LLProps.ProtocolName }}::{{ .Name }}Request*>(bytes);
  {{- end }}
  interface->{{ .Name }}({{ template "SyncServerDispatchMoveParams" .Request }}{{ if .Request }},{{ end }}
                         {{ .LLProps.ProtocolName }}::Interface::{{ .Name }}Completer::Sync(txn));
}
  {{- end }}
{{- end }}

}  // namespace methods

namespace entries {

::fidl::internal::InterfaceEntry<{{ .Name }}::Interface> {{ .Name }}[] = {
{{- range .Methods }}
  {{- if .HasRequest }}
  { {{ .OrdinalName }}, {{ .LLProps.ProtocolName }}::{{ .Name}}Request::Type,
    methods::{{ .LLProps.ProtocolName }}Dispatch{{ .Name }} },
  {{- end }}
{{- end }}
};

}  // namespace entries

bool {{ .Name }}::TryDispatch{{ template "SyncServerDispatchMethodSignature" }} {
  return ::fidl::internal::TryDispatch(
      impl, msg, txn,
      reinterpret_cast<::fidl::internal::InterfaceEntry<void>*>(entries::{{ .Name }}),
      reinterpret_cast<::fidl::internal::InterfaceEntry<void>*>(
          entries::{{ .Name }} +
          sizeof(entries::{{ .Name }}) /
              sizeof(::fidl::internal::InterfaceEntry<{{ .Name }}::Interface>)));
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
