// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentClientTmpl = `
{{- define "ClientForwardDeclaration" }}

#ifdef __Fuchsia__
  using AsyncEventHandler = {{ .WireAsyncEventHandler }};
  {{- range .TwoWayMethods }}
  class {{ .WireResponseContext.Self }};
  {{- end }}
  using ClientImpl = {{ .WireClientImpl }};
#endif
{{- end }}

{{- define "ClientDeclaration" }}
#ifdef __Fuchsia__
{{ PushNamespace }}
{{- EnsureNamespace "" }}
template<>
class {{ .WireAsyncEventHandler }} : public {{ .WireEventHandlerInterface }} {
 public:
 {{ .WireAsyncEventHandler.Self }}() = default;

  virtual void Unbound(::fidl::UnbindInfo info) {}
};

{{ EnsureNamespace . }}
{{- range .TwoWayMethods }}
{{ "" }}
class {{ .WireResponseContext }} : public ::fidl::internal::ResponseContext {
 public:
  {{ .WireResponseContext.Self }}();

  virtual void OnReply({{ .WireResponse }}* message) = 0;

 private:
  void OnReply(uint8_t* reply) override;
};
{{- end }}

{{- EnsureNamespace "" }}
template<>
class {{ .WireClientImpl }} final : private ::fidl::internal::ClientBase {
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
  // Asynchronous variant of |{{ $.Name }}.{{ .Name }}()|.
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
  // Asynchronous variant of |{{ $.Name }}.{{ .Name }}()|.
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
  // Synchronous variant of |{{ $.Name }}.{{ .Name }}()|.
  // {{- template "ClientAllocationComment" . }}
  {{ .WireResultOf }} {{ .Name }}_Sync({{ .RequestArgs | Params }});

    {{- /* Sync caller-allocate flavor */}}
    {{- if or .RequestArgs .ResponseArgs }}
{{ "" }}
      {{- range .DocComments }}
  //{{ . }}
      {{- end }}
      {{- if .DocComments }}
  //
      {{- end }}
  // Synchronous variant of |{{ $.Name }}.{{ .Name }}()|.
  // Caller provides the backing storage for FIDL message via request and
  // response buffers.
  {{ .WireUnownedResultOf }} {{ .Name }}{{ if .HasResponse }}_Sync{{ end }}(
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
  ::fidl::Result {{ .Name }}({{ .RequestArgs | Params }});

    {{- /* Caller-allocate flavor */}}
    {{- if .RequestArgs }}
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

  {{ .WireAsyncEventHandler }}* event_handler() const { return event_handler_.get(); }

 private:
  friend class ::fidl::Client<{{ . }}>;
  friend class ::fidl::internal::ControlBlock<{{ . }}>;

  explicit WireClientImpl(std::shared_ptr<{{ .WireAsyncEventHandler }}> event_handler)
      : event_handler_(std::move(event_handler)) {}

  std::optional<::fidl::UnbindInfo> DispatchEvent(fidl_incoming_msg_t* msg) override;

  std::shared_ptr<{{ .WireAsyncEventHandler }}> event_handler_;
};
{{ PopNamespace }}
#endif
{{- end }}

{{- define "ClientDispatchDefinition" }}
{{ EnsureNamespace ""}}
#ifdef __Fuchsia__
std::optional<::fidl::UnbindInfo> {{ .WireClientImpl.NoLeading }}::DispatchEvent(fidl_incoming_msg_t* msg) {
  {{- if .Events }}
  if (event_handler_ != nullptr) {
    fidl_message_header_t* hdr = reinterpret_cast<fidl_message_header_t*>(msg->bytes);
    switch (hdr->ordinal) {
    {{- range .Events }}
      case {{ .OrdinalName }}:
      {
        const char* error_message;
        zx_status_t status = fidl_decode_etc({{ .WireResponse }}::Type, msg->bytes, msg->num_bytes,
                                             msg->handles, msg->num_handles, &error_message);
        if (status != ZX_OK) {
          return ::fidl::UnbindInfo{::fidl::UnbindInfo::kDecodeError, status};
        }
        event_handler_->{{ .Name }}(reinterpret_cast<{{ .WireResponse }}*>(msg->bytes));
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
#endif
{{- end }}
`
