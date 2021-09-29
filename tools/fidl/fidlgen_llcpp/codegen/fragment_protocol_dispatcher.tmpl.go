// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

// fragmentProtocolDispatcherTmpl contains the definition for
// fidl::internal::WireDispatcher<Protocol>.
const fragmentProtocolDispatcherTmpl = `
{{- define "Protocol:Dispatcher:MessagingHeader" }}
{{- IfdefFuchsia }}
{{- EnsureNamespace "" }}
template<>
struct {{ .WireServerDispatcher }} final {
  {{ .WireServerDispatcher.Self }}() = delete;
  static ::fidl::DispatchResult TryDispatch({{ .WireServer }}* impl, ::fidl::IncomingMessage& msg,
                                            ::fidl::Transaction* txn);
  static void Dispatch({{ .WireServer }}* impl, ::fidl::IncomingMessage&& msg,
                       ::fidl::Transaction* txn);

 private:
  static const ::fidl::internal::MethodEntry entries_[];
  static const ::fidl::internal::MethodEntry* entries_end_;
};
{{- EndifFuchsia }}
{{- end }}

{{- define "Protocol:Dispatcher:MessagingSource" }}
{{- IfdefFuchsia -}}
{{ EnsureNamespace "" }}

constexpr ::fidl::internal::MethodEntry {{ .WireServerDispatcher.NoLeading }}::entries_[] = {
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

const ::fidl::internal::MethodEntry* {{ .WireServerDispatcher.NoLeading }}::entries_end_ =
    &entries_[{{- len .ClientMethods -}}];

::fidl::DispatchResult {{ .WireServerDispatcher.NoLeading }}::TryDispatch(
    {{ .WireServer }}* impl, ::fidl::IncomingMessage& msg, ::fidl::Transaction* txn) {
  return ::fidl::internal::TryDispatch(impl, msg, txn, entries_, entries_end_);
}

{{ EnsureNamespace "" }}
void {{ .WireServerDispatcher.NoLeading }}::Dispatch(
    {{- .WireServer }}* impl, ::fidl::IncomingMessage&& msg, ::fidl::Transaction* txn) {
  ::fidl::internal::Dispatch(impl, msg, txn, entries_, entries_end_);
}

{{- EnsureNamespace "" }}
void {{ .WireServer.NoLeading }}::dispatch_message(
    fidl::IncomingMessage&& msg, ::fidl::Transaction* txn) {
  {{ .WireServerDispatcher }}::Dispatch(this, std::move(msg), txn);
}
{{- EndifFuchsia -}}

{{- end }}
`
