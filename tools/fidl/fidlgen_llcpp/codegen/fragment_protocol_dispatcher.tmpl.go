// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentProtocolDispatcherTmpl = `
{{- define "ProtocolDispatcherDeclaration" }}
{{- IfdefFuchsia }}
{{- EnsureNamespace "" }}
template<>
struct {{ .WireDispatcher }} final {
  {{ .WireDispatcher.Self }}() = delete;
  static ::fidl::DispatchResult TryDispatch({{ .WireInterface }}* impl, fidl_incoming_msg_t* msg, ::fidl::Transaction* txn);
  static ::fidl::DispatchResult Dispatch({{ .WireInterface }}* impl, fidl_incoming_msg_t* msg, ::fidl::Transaction* txn);
};
{{- EndifFuchsia }}
{{- end }}



{{- define "ProtocolDispatcherDefinition" }}
{{- IfdefFuchsia -}}
{{ EnsureNamespace "" }}
::fidl::DispatchResult {{ .WireDispatcher.NoLeading }}::TryDispatch({{ .WireInterface }}* impl, fidl_incoming_msg_t* msg, ::fidl::Transaction* txn) {
  {{- if .ClientMethods }}
  static const ::fidl::internal::MethodEntry entries[] = {
    {{- range .ClientMethods }}
      { {{ .OrdinalName }}, {{ .WireRequest }}::Type,
        [](void* interface, void* bytes, ::fidl::Transaction* txn) {
          {{- if .RequestArgs }}
            auto message = reinterpret_cast<{{ .WireRequest }}*>(bytes);
          {{- end }}
          {{ .WireCompleter }}::Sync completer(txn);
          reinterpret_cast<{{ .Protocol }}::Interface*>(interface)->{{ .Name }}(
                {{- range $index, $param := .RequestArgs }}
                  std::move(message->{{ $param.Name }}),
                {{- end }}            
                completer);        
        },
      },
    {{- end }}
  };  
  return ::fidl::internal::TryDispatch(
      impl, msg, txn,
      entries, entries + sizeof(entries) / sizeof(::fidl::internal::MethodEntry));
  {{- else }}
    return ::fidl::DispatchResult::kNotFound;
  {{- end }}
}

{{ EnsureNamespace "" }}
::fidl::DispatchResult {{ .WireDispatcher.NoLeading }}::Dispatch({{ .WireInterface }}* impl, fidl_incoming_msg_t* msg, ::fidl::Transaction* txn) {
  {{- if .ClientMethods }}
  ::fidl::DispatchResult dispatch_result = TryDispatch(impl, msg, txn);
  if (dispatch_result == ::fidl::DispatchResult::kNotFound) {
    FidlHandleInfoCloseMany(msg->handles, msg->num_handles);
    txn->InternalError({::fidl::UnbindInfo::kUnexpectedMessage, ZX_ERR_NOT_SUPPORTED});
  }
  return dispatch_result;
  {{- else }}
  FidlHandleInfoCloseMany(msg->handles, msg->num_handles);
  txn->InternalError({::fidl::UnbindInfo::kUnexpectedMessage, ZX_ERR_NOT_SUPPORTED});
  return ::fidl::DispatchResult::kNotFound;
  {{- end }}
}

// TODO(ianloic): Remove this when all users have migrated.
::fidl::DispatchResult {{ .NoLeading }}::Dispatch({{ .WireInterface }}* impl, fidl_incoming_msg_t* msg, ::fidl::Transaction* txn) {
  return {{ .WireDispatcher }}::Dispatch(impl, msg, txn);
}

{{- EnsureNamespace "" }}
::fidl::DispatchResult {{ .WireInterface.NoLeading }}::dispatch_message(fidl_incoming_msg_t* msg,
                                                         ::fidl::Transaction* txn) {
  return {{ .WireDispatcher }}::Dispatch(this, msg, txn);
}
{{- EndifFuchsia -}}

{{- end }}
`
