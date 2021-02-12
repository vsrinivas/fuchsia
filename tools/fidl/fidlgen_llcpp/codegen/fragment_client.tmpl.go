// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentClientTmpl = `
{{- define "ClientForwardDeclaration" }}
  class AsyncEventHandler;
  {{- range .TwoWayMethods }}
  class {{ .Name }}ResponseContext;
  {{- end }}
  class ClientImpl;
{{- end }}

{{- define "ClientDeclaration" }}
{{ EnsureNamespace . }}
{{- $outer := . }}
class {{ .Name }}::AsyncEventHandler : public {{ .Name }}::EventHandlerInterface {
 public:
  AsyncEventHandler() = default;

  virtual void Unbound(::fidl::UnbindInfo info) {}
};

{{- range .TwoWayMethods }}
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
  {{- /* Generate both sync and async flavors for two-way methods. */}}
  {{- range .TwoWayMethods }}

    {{- /* Async managed flavor */}}
    {{- range .DocComments }}
  //{{ . }}
    {{- end }}
    {{- if .DocComments }}
  //
    {{- end }}
  // Asynchronous variant of |{{ $outer.Name }}.{{ .Name }}()|.
  // {{ template "AsyncClientAllocationComment" . }}
  ::fidl::Result {{ .Name }}({{ template "ClientAsyncRequestManagedMethodArguments" . }});
{{ "" }}

    {{- /* Async caller-allocate flavor */}}
    {{- range .DocComments }}
  //{{ . }}
    {{- end }}
    {{- if .DocComments }}
  //
    {{- end }}
  // Asynchronous variant of |{{ $outer.Name }}.{{ .Name }}()|.
  // Caller provides the backing storage for FIDL message via request buffer.
  // Ownership of |_context| is given unsafely to the binding until |OnError|
  // or |OnReply| are called on it.
  ::fidl::Result {{ .Name }}({{ template "ClientAsyncRequestCallerAllocateMethodArguments" . }});
{{ "" }}

    {{- /* Sync managed flavor */}}
    {{- range .DocComments }}
  //{{ . }}
    {{- end }}
    {{- if .DocComments }}
  //
    {{- end }}
  // Synchronous variant of |{{ $outer.Name }}.{{ .Name }}()|.
  // {{- template "ClientAllocationComment" . }}
  ResultOf::{{ .Name }} {{ .Name }}_Sync(
      {{- template "SyncRequestManagedMethodArguments" . }});

    {{- /* Sync caller-allocate flavor */}}
    {{- if or .Request .Response }}
{{ "" }}
      {{- range .DocComments }}
  //{{ . }}
      {{- end }}
      {{- if .DocComments }}
  //
      {{- end }}
  // Synchronous variant of |{{ $outer.Name }}.{{ .Name }}()|.
  // Caller provides the backing storage for FIDL message via request and
  // response buffers.
  UnownedResultOf::{{ .Name }} {{ .Name }}{{ if .HasResponse }}_Sync{{ end }}(
      {{- template "SyncRequestCallerAllocateMethodArguments" . }});
    {{- end }}
{{ "" }}
  {{- end }}

  {{- /* There is no distinction between sync vs async for one-way methods . */}}
  {{- range .OneWayMethods }}
    {{- /* Managed flavor */}}
    {{- range .DocComments }}
  //{{ . }}
    {{- end }}
    {{- if .DocComments }}
  //
    {{- end }}
  // {{- template "ClientAllocationComment" . }}
  ::fidl::Result {{ .Name }}({{- template "SyncRequestManagedMethodArguments" . }});

    {{- /* Caller-allocate flavor */}}
    {{- if .Request }}
{{ "" }}
      {{- range .DocComments }}
  //{{ . }}
      {{- end }}
      {{- if .DocComments }}
  //
      {{- end }}
  // Caller provides the backing storage for FIDL message via request buffer.
  ::fidl::Result {{ .Name }}({{- template "SyncRequestCallerAllocateMethodArguments" . }});
    {{- end }}
{{ "" }}
  {{- end }}

  AsyncEventHandler* event_handler() const { return event_handler_.get(); }

 private:
  friend class ::fidl::Client<{{ .Name }}>;
  friend class ::fidl::internal::ControlBlock<{{ .Name }}>;

  explicit ClientImpl(std::shared_ptr<AsyncEventHandler> event_handler)
      : event_handler_(std::move(event_handler)) {}

  std::optional<::fidl::UnbindInfo> DispatchEvent(fidl_incoming_msg_t* msg) override;

  std::shared_ptr<AsyncEventHandler> event_handler_;
};
{{- end }}

{{- define "ClientDispatchDefinition" }}
std::optional<::fidl::UnbindInfo> {{ .Name }}::ClientImpl::DispatchEvent(fidl_incoming_msg_t* msg) {
  {{- if .Events }}
  if (event_handler_ != nullptr) {
    fidl_message_header_t* hdr = reinterpret_cast<fidl_message_header_t*>(msg->bytes);
    switch (hdr->ordinal) {
    {{- range .Events }}
      case {{ .OrdinalName }}:
      {
        const char* error_message;
        zx_status_t status = fidl_decode_etc({{ .Name }}Response::Type, msg->bytes, msg->num_bytes,
                                             msg->handles, msg->num_handles, &error_message);
        if (status != ZX_OK) {
          return ::fidl::UnbindInfo{::fidl::UnbindInfo::kDecodeError, status};
        }
        event_handler_->{{ .Name }}(reinterpret_cast<{{ .Name }}Response*>(msg->bytes));
        return std::nullopt;
      }
    {{- end }}
      default:
        break;
    }
  }
  {{- end }}
  FidlHandleInfoCloseMany(msg->handles, msg->num_handles);
  return ::fidl::UnbindInfo{::fidl::UnbindInfo::kUnexpectedMessage, ZX_ERR_NOT_SUPPORTED};
}
{{- end }}
`
