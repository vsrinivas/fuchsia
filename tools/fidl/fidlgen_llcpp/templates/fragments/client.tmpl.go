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
  std::variant<::fit::function<void {{- template "AsyncEventHandlerIndividualMethodSignature" . }}>,
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
  {{ .Name }}ResponseContext();

  virtual void OnReply({{ $outer.Name }}::{{ .Name }}Response* message) = 0;

 private:
  void OnReply(uint8_t* reply) override;
};
{{- end }}

class {{ .Name }}::ClientImpl final : private ::fidl::internal::ClientBase {
 public:
  {{- range FilterMethodsWithoutReqs .Methods -}}
    {{- if .HasResponse -}}
      {{- range .DocComments }}
  //{{ . }}
      {{- end }}
{{ "" }}
  // Asynchronous variant of |{{ $outer.Name }}.{{ .Name }}()|. {{ template "AsyncClientAllocationComment" . }}
  ::fidl::Result {{ .Name }}({{ template "ClientAsyncRequestManagedMethodArguments" . }});
      {{- range .DocComments }}
  //{{ . }}
      {{- end }}
  // Asynchronous variant of |{{ $outer.Name }}.{{ .Name }}()|. Caller provides the backing storage for FIDL message via request buffer. Ownership of _context is given unsafely to the binding until OnError() or OnReply() are called on it.
  ::fidl::Result {{ .Name }}({{ template "ClientAsyncRequestCallerAllocateMethodArguments" . }});
    {{- end }}
    {{- range .DocComments }}
  //{{ . }}
    {{- end }}
  // {{- if .HasResponse }} Synchronous variant of |{{ $outer.Name }}.{{ .Name }}()|. {{- end }}{{ template "ClientAllocationComment" . }}
  {{ if .HasResponse }}ResultOf::{{ .Name }}{{ else }}::fidl::Result{{ end }} {{ .Name }}{{ if .HasResponse }}_Sync{{ end }}({{ template "SyncRequestManagedMethodArguments" . }});
    {{- if or .Request .Response }}
{{ "" }}
      {{- range .DocComments }}
  //{{ . }}
      {{- end }}
  // {{- if .HasResponse }} Synchronous variant of |{{ $outer.Name }}.{{ .Name }}()|. {{- end }} Caller provides the backing storage for FIDL message via request and response buffers.
  {{ if .HasResponse }}UnownedResultOf::{{ .Name }}{{ else }}::fidl::Result{{ end }} {{ .Name }}{{ if .HasResponse }}_Sync{{ end }}({{ template "SyncRequestCallerAllocateMethodArguments" . }});
    {{- end }}
{{ "" }}
  {{- end }}
 private:
  friend class ::fidl::Client<{{ .Name }}>;

  ClientImpl(::zx::channel client_end, async_dispatcher_t* dispatcher,
             ::fidl::internal::TypeErasedOnUnboundFn on_unbound, AsyncEventHandlers handlers)
      : ::fidl::internal::ClientBase(std::move(client_end), dispatcher, std::move(on_unbound)),
        handlers_(std::move(handlers)) {}

  std::optional<::fidl::UnbindInfo> DispatchEvent(fidl_msg_t* msg) override;

  AsyncEventHandlers handlers_;
};
{{- end }}

{{- define "ClientDispatchDefinition" }}
std::optional<::fidl::UnbindInfo> {{ .Name }}::ClientImpl::DispatchEvent(fidl_msg_t* msg) {
  fidl_message_header_t* hdr = reinterpret_cast<fidl_message_header_t*>(msg->bytes);
  switch (hdr->ordinal) {
  {{- range FilterMethodsWithoutResps .Methods }}
    {{- if not .HasRequest }}
    case {{ .OrdinalName }}:
    {
      auto result = ::fidl::DecodeAs<{{ .Name }}Response>(msg);
      if (result.status != ZX_OK) {
        return ::fidl::UnbindInfo{::fidl::UnbindInfo::kDecodeError, result.status};
      }
        {{- if .Response }}
      if (auto* managed = std::get_if<0>(&handlers_.{{ .NameInLowerSnakeCase }})) {
        if (!(*managed))
          return ::fidl::UnbindInfo{::fidl::UnbindInfo::kUnexpectedMessage, ZX_ERR_NOT_SUPPORTED};
        auto message = result.message.message();
        (*managed)({{ template "AsyncEventHandlerMoveParams" .Response }});
      } else {
        std::get<1>(handlers_.{{ .NameInLowerSnakeCase }})(std::move(result.message));
      }
        {{- else }}
      if (!handlers_.{{ .NameInLowerSnakeCase }})
        return ::fidl::UnbindInfo{::fidl::UnbindInfo::kUnexpectedMessage, ZX_ERR_NOT_SUPPORTED};
      handlers_.{{ .NameInLowerSnakeCase }}();
        {{- end }}
      break;
    }
    {{- end }}
  {{- end }}
    default:
      zx_handle_close_many(msg->handles, msg->num_handles);
      return ::fidl::UnbindInfo{::fidl::UnbindInfo::kUnexpectedMessage, ZX_ERR_NOT_SUPPORTED};
  }
  return {};
}
{{- end }}
`
