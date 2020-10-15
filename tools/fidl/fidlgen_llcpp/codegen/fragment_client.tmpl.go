// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentClientTmpl = `
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
  ::fit::function<void (
    {{- if .Response -}}
      {{ .Name }}Response* msg
    {{- end -}}
  )> {{ .NameInLowerSnakeCase }};
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

  explicit ClientImpl(AsyncEventHandlers handlers) : handlers_(std::move(handlers)) {}

  std::optional<::fidl::UnbindInfo> DispatchEvent(fidl_incoming_msg_t* msg) override;

  AsyncEventHandlers handlers_;
};
{{- end }}

{{- define "ClientDispatchDefinition" }}
std::optional<::fidl::UnbindInfo> {{ .Name }}::ClientImpl::DispatchEvent(fidl_incoming_msg_t* msg) {
  fidl_message_header_t* hdr = reinterpret_cast<fidl_message_header_t*>(msg->bytes);
  switch (hdr->ordinal) {
  {{- range FilterMethodsWithoutResps .Methods }}
    {{- if not .HasRequest }}
    case {{ .OrdinalName }}:
    {
      const char* error_message;
      zx_status_t status = fidl_decode({{ .Name }}Response::Type, msg->bytes, msg->num_bytes,
                                       msg->handles, msg->num_handles, &error_message);
      if (status != ZX_OK) {
        return ::fidl::UnbindInfo{::fidl::UnbindInfo::kDecodeError, status};
      }
      if (!handlers_.{{ .NameInLowerSnakeCase }}) {
        return ::fidl::UnbindInfo{::fidl::UnbindInfo::kUnexpectedMessage, ZX_ERR_NOT_SUPPORTED};
      }
      handlers_.{{ .NameInLowerSnakeCase }}(
        {{- if .Response -}}
        reinterpret_cast<{{ .Name }}Response*>(msg->bytes)
        {{- end -}}
      );
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
