// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fragments

const Client = `
{{- define "ClientForwardDeclaration" }}
  struct AsyncEventHandlers;
  {{- range FilterMethodsWithoutReqs .Methods | FilterMethodsWithoutResps }}
  class {{ .Name }}ResponseContext;
  {{- end }}
  class ClientImpl;
{{- end }}

{{- define "ClientDeclaration" }}
{{- $outer := . }}
struct {{ .Name }}::AsyncEventHandlers {
  {{- range FilterMethodsWithReqs .Methods -}}
    {{- range .DocComments }}
  //{{ . }}
    {{- end }}
    {{- if .Response }}
  std::variant<::fit::function<void {{- template "SyncEventHandlerIndividualMethodSignature" . }}>,
               ::fit::function<void {{- template "AsyncEventHandlerInPlaceMethodSignature" . }}>> {{ .NameInLowerSnakeCase }};
    {{- else }}
  ::fit::function<void()> {{ .NameInLowerSnakeCase }};
    {{- end }}
{{ "" }}
  {{- end }}
};

{{- range FilterMethodsWithoutReqs .Methods | FilterMethodsWithoutResps }}
{{ "" }}
class {{ $outer.Name }}::{{ .Name }}ResponseContext : public ::fidl::internal::ResponseContext {
 public:
  virtual ~{{ .Name }}ResponseContext() = default;
  virtual void OnReply {{- template "AsyncEventHandlerInPlaceMethodSignature" . }} = 0;

 protected:
 {{ .Name }}ResponseContext() = default;
};
{{- end }}

class {{ .Name }}::ClientImpl final : private ::fidl::internal::ClientBase {
 public:
  {{- range FilterMethodsWithoutReqs .Methods -}}
    {{- if .HasResponse -}}
      {{- range .DocComments }}
  //{{ . }}
      {{- end }}
  // Asynchronous variant of |{{ $outer.Name }}.{{ .Name }}()|. {{ template "AsyncClientAllocationComment" . }}
  ::fidl::StatusAndError {{ .Name }}({{ template "ClientAsyncRequestManagedMethodArguments" . }});
{{ "" }}
      {{- range .DocComments }}
  //{{ . }}
      {{- end }}
  // Asynchronous variant of |{{ $outer.Name }}.{{ .Name }}()|. Caller provides the backing storage for FIDL message via request and response buffers. Ownership of _context is given unsafely to the binding until OnError() or OnReply() are called on it.
  ::fidl::StatusAndError {{ .Name }}({{ template "ClientAsyncRequestCallerAllocateMethodArguments" . }});
    {{- end }}
    {{- range .DocComments }}
  //{{ . }}
    {{- end }}
  // {{- if .HasResponse }} Synchronous variant of |{{ $outer.Name }}.{{ .Name }}()|. {{- end }}{{ template "ClientAllocationComment" . }}
  {{ if .HasResponse }}ResultOf::{{ .Name }}{{ else }}::fidl::StatusAndError{{ end }} {{ .Name }}{{ if .HasResponse }}_Sync{{ end }}({{ template "SyncRequestManagedMethodArguments" . }});
    {{- if or .Request .Response }}
{{ "" }}
      {{- range .DocComments }}
  //{{ . }}
      {{- end }}
  // {{- if .HasResponse }} Synchronous variant of |{{ $outer.Name }}.{{ .Name }}()|. {{- end }} Caller provides the backing storage for FIDL message via request and response buffers.
  {{ if .HasResponse }}UnownedResultOf::{{ .Name }}{{ else }}::fidl::StatusAndError{{ end }} {{ .Name }}{{ if .HasResponse }}_Sync{{ end }}({{ template "SyncRequestCallerAllocateMethodArguments" . }});
    {{- end }}
{{ "" }}
  {{- end }}
 private:
  friend class ::fidl::Client<{{ .Name }}>;

  ClientImpl(::zx::channel client_end, async_dispatcher_t* dispatcher,
             ::fidl::internal::TypeErasedOnUnboundFn on_unbound, AsyncEventHandlers handlers)
      : ::fidl::internal::ClientBase(std::move(client_end), dispatcher, std::move(on_unbound)),
        handlers_(std::move(handlers)) {}

  std::optional<::fidl::UnbindInfo> Dispatch(fidl_msg_t* msg,
                                             ::fidl::internal::ResponseContext* context) override;

  AsyncEventHandlers handlers_;
};
{{- end }}

{{- define "ClientDispatchDefinition" }}
{{- if FilterMethodsWithoutResps .Methods }}
std::optional<::fidl::UnbindInfo> {{ .Name }}::ClientImpl::Dispatch(
    fidl_msg_t* msg, ::fidl::internal::ResponseContext* context) {
  fidl_message_header_t* hdr = reinterpret_cast<fidl_message_header_t*>(msg->bytes);
  switch (hdr->ordinal) {
  {{- range FilterMethodsWithoutResps .Methods }}
    case {{ .OrdinalName }}:
    {
      auto result = ::fidl::DecodeAs<{{ .Name }}Response>(msg);
      if (result.status != ZX_OK) {
        {{- if .HasRequest }}
        context->OnError();
        {{- end }}
	return ::fidl::UnbindInfo{::fidl::UnbindInfo::kDecodeError, result.status};
      }
      {{- if .HasRequest }}
      static_cast<{{ .Name }}ResponseContext*>(context)->OnReply({{- if .Response -}} std::move(result.message) {{- end -}});
      {{- else }}
        {{- if .Response }}
      if (auto* managed = std::get_if<0>(&handlers_.{{ .NameInLowerSnakeCase }})) {
        if (!(*managed))
          return ::fidl::UnbindInfo{::fidl::UnbindInfo::kUnexpectedMessage, ZX_ERR_NOT_SUPPORTED};
        auto message = result.message.message();
        (*managed)({{ template "SyncEventHandlerMoveParams" .Response }});
      } else {
        std::get<1>(handlers_.{{ .NameInLowerSnakeCase }})(std::move(result.message));
      }
        {{- else }}
      if (!handlers_.{{ .NameInLowerSnakeCase }})
        return ::fidl::UnbindInfo{::fidl::UnbindInfo::kUnexpectedMessage, ZX_ERR_NOT_SUPPORTED};
      handlers_.{{ .NameInLowerSnakeCase }}();
	{{- end }}
      {{- end }}
      break;
    }
  {{- end }}
    case kFidlOrdinalEpitaph:
      zx_handle_close_many(msg->handles, msg->num_handles);
      if (context)
        return ::fidl::UnbindInfo{::fidl::UnbindInfo::kUnexpectedMessage, ZX_ERR_INVALID_ARGS};
      return ::fidl::UnbindInfo{::fidl::UnbindInfo::kPeerClosed,
                                reinterpret_cast<fidl_epitaph_t*>(hdr)->error};
    default:
      zx_handle_close_many(msg->handles, msg->num_handles);
      if (context) context->OnError();
      return ::fidl::UnbindInfo{::fidl::UnbindInfo::kUnexpectedMessage, ZX_ERR_NOT_SUPPORTED};
  }
  return {};
}
{{- else }}
std::optional<::fidl::UnbindInfo> {{ .Name }}::ClientImpl::Dispatch(fidl_msg_t*, ::fidl::internal::ResponseContext*) {
  return {};
}
{{- end }}
{{- end }}
`
