// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

// fragmentProtocolDispatcherTmpl contains the definition for
// fidl::internal::WireDispatcher<Protocol>.
const fragmentProtocolDispatcherTmpl = `
{{- define "ProtocolDispatcherDeclaration" }}
{{- IfdefFuchsia }}
{{- EnsureNamespace "" }}
template<>
struct {{ .WireDispatcher }} final {
  {{ .WireDispatcher.Self }}() = delete;
  static ::fidl::DispatchResult TryDispatch(
      {{- .WireInterface }}* impl, fidl::IncomingMessage& msg, ::fidl::Transaction* txn);
  static ::fidl::DispatchResult Dispatch(
      {{- .WireInterface }}* impl, fidl::IncomingMessage&& msg, ::fidl::Transaction* txn);
};

template<>
struct {{ .WireServerDispatcher }} final {
  {{ .WireServerDispatcher.Self }}() = delete;
  static ::fidl::DispatchResult TryDispatch({{ .WireServer }}* impl, ::fidl::IncomingMessage& msg,
                                            ::fidl::Transaction* txn);
  static ::fidl::DispatchResult Dispatch({{ .WireServer }}* impl, ::fidl::IncomingMessage&& msg,
                                         ::fidl::Transaction* txn);
};
{{- EndifFuchsia }}
{{- end }}



{{- define "ProtocolDispatcherDefinition" }}
{{- IfdefFuchsia -}}
{{ EnsureNamespace "" }}
::fidl::DispatchResult {{ .WireServerDispatcher.NoLeading }}::TryDispatch(
    {{ .WireServer }}* impl, ::fidl::IncomingMessage& msg, ::fidl::Transaction* txn) {
  {{- if .ClientMethods }}
  static const ::fidl::internal::MethodEntry entries[] = {
    {{- range .ClientMethods }}
      { {{ .OrdinalName }},
        [](void* interface, ::fidl::IncomingMessage&& msg, ::fidl::Transaction* txn) {
          {{- if .RequestArgs }}
          ::fidl::DecodedMessage<{{ .WireRequest }}> decoded{std::move(msg)};
          if (unlikely(!decoded.ok())) {
            return decoded.status();
          }
          auto* primary = decoded.PrimaryObject();
          {{- else }}
          auto* primary = reinterpret_cast<{{ .WireRequest }}*>(msg.bytes());
          {{- end }}
          {{ .WireCompleter }}::Sync completer(txn);
          reinterpret_cast<{{ $.WireServer }}*>(interface)->{{ .Name }}(
              primary, completer);
          return ZX_OK;
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
::fidl::DispatchResult {{ .WireServerDispatcher.NoLeading }}::Dispatch(
    {{- .WireServer }}* impl, ::fidl::IncomingMessage&& msg, ::fidl::Transaction* txn) {
  {{- if .ClientMethods }}
  ::fidl::DispatchResult dispatch_result = TryDispatch(impl, msg, txn);
  if (unlikely(dispatch_result == ::fidl::DispatchResult::kNotFound)) {
    std::move(msg).CloseHandles();
    txn->InternalError(::fidl::UnbindInfo::UnknownOrdinal());
  }
  return dispatch_result;
  {{- else }}
  std::move(msg).CloseHandles();
  txn->InternalError(::fidl::UnbindInfo::UnknownOrdinal());
  return ::fidl::DispatchResult::kNotFound;
  {{- end }}
}

{{- EnsureNamespace "" }}
::fidl::DispatchResult {{ .WireServer.NoLeading }}::dispatch_message(
    fidl::IncomingMessage&& msg, ::fidl::Transaction* txn) {
  return {{ .WireServerDispatcher }}::Dispatch(this, std::move(msg), txn);
}

::fidl::DispatchResult {{ .WireDispatcher.NoLeading }}::TryDispatch(
    {{ .WireInterface }}* impl, ::fidl::IncomingMessage& msg, ::fidl::Transaction* txn) {
  {{- if .ClientMethods }}
  static const ::fidl::internal::MethodEntry entries[] = {
    {{- range .ClientMethods }}
      { {{ .OrdinalName }},
        [](void* interface, ::fidl::IncomingMessage&& msg, ::fidl::Transaction* txn) {
          {{- if .RequestArgs }}
          ::fidl::DecodedMessage<{{ .WireRequest }}> decoded{std::move(msg)};
          if (unlikely(!decoded.ok())) {
            return decoded.status();
          }
          auto* primary = decoded.PrimaryObject();
          {{- end }}
          {{ .WireCompleter }}::Sync completer(txn);
          reinterpret_cast<{{ .Protocol.WireInterface }}*>(interface)->{{ .Name }}(
                {{- range $index, $param := .RequestArgs }}
                  std::move(primary->{{ $param.Name }}),
                {{- end }}
                completer);
          return ZX_OK;
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
::fidl::DispatchResult
{{ .WireDispatcher.NoLeading }}::Dispatch(
    {{- .WireInterface }}* impl, fidl::IncomingMessage&& msg, ::fidl::Transaction* txn) {
  {{- if .ClientMethods }}
  ::fidl::DispatchResult dispatch_result = TryDispatch(impl, msg, txn);
  if (unlikely(dispatch_result == ::fidl::DispatchResult::kNotFound)) {
    std::move(msg).CloseHandles();
    txn->InternalError(::fidl::UnbindInfo::UnknownOrdinal());
  }
  return dispatch_result;
  {{- else }}
  std::move(msg).CloseHandles();
  txn->InternalError(::fidl::UnbindInfo::UnknownOrdinal());
  return ::fidl::DispatchResult::kNotFound;
  {{- end }}
}

{{- EnsureNamespace "" }}
::fidl::DispatchResult {{ .WireInterface.NoLeading }}::dispatch_message(
    fidl::IncomingMessage&& msg, ::fidl::Transaction* txn) {
  return {{ .WireDispatcher }}::Dispatch(this, std::move(msg), txn);
}
{{- EndifFuchsia -}}

{{- end }}
`
