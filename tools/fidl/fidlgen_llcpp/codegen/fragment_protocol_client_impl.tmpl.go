// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentProtocolClientImplTmpl = `
{{- define "ProtocolClientImplDeclaration" }}
{{- IfdefFuchsia -}}
{{- EnsureNamespace "" }}
template<>
class {{ .WireClientImpl }} final : private ::fidl::internal::ClientBase {
 public:
  {{- /* Generate both sync and async flavors for two-way methods. */}}
  {{- range .TwoWayMethods }}

    {{- /* Async managed flavor */}}
    {{- .Docs }}
    {{- if .DocComments }}
  //
    {{- end }}
  // Asynchronous variant of |{{ $.Name }}.{{ .Name }}()|.
  // {{ template "AsyncClientAllocationComment" . }}
  ::fidl::Result {{ .Name }}(
        {{ RenderCalleeParams .RequestArgs
                        (printf "::fit::callback<void (%s* response)> _cb" .WireResponse) }});
{{ "" }}

    {{- /* Async caller-allocate flavor */}}
    {{- .Docs }}
    {{- if .DocComments }}
  //
    {{- end }}
  // Asynchronous variant of |{{ $.Name }}.{{ .Name }}()|.
  // Caller provides the backing storage for FIDL message via request buffer.
  // Ownership of |_context| is given unsafely to the binding until |OnError|
  // or |OnReply| are called on it.
  ::fidl::Result {{ .Name }}(
        {{- if .RequestArgs }}
          {{ RenderCalleeParams "::fidl::BufferSpan _request_buffer"
                          .RequestArgs
                          (printf "%s* _context" .WireResponseContext) }}
        {{- else }}
          {{ .WireResponseContext }}* _context
        {{- end -}}
    );
{{ "" }}

    {{- /* Sync managed flavor */}}
    {{- .Docs }}
    {{- if .DocComments }}
  //
    {{- end }}
  // Synchronous variant of |{{ $.Name }}.{{ .Name }}()|.
  // {{- template "ClientAllocationComment" . }}
  {{ .WireResult }} {{ .Name }}_Sync({{ RenderCalleeParams .RequestArgs }});

    {{- /* Sync caller-allocate flavor */}}
    {{- if or .RequestArgs .ResponseArgs }}
{{ "" }}
      {{- .Docs }}
      {{- if .DocComments }}
  //
      {{- end }}
  // Synchronous variant of |{{ $.Name }}.{{ .Name }}()|.
  // Caller provides the backing storage for FIDL message via request and
  // response buffers.
  {{ .WireUnownedResult }} {{ .Name }}{{ if .HasResponse }}_Sync{{ end }}(
      {{- template "SyncRequestCallerAllocateMethodArguments" . }});
    {{- end }}
{{ "" }}
  {{- end }}

  {{- /* There is no distinction between sync vs async for one-way methods . */}}
  {{- range .OneWayMethods }}
    {{- /* Managed flavor */}}
    {{- .Docs }}
    {{- if .DocComments }}
  //
    {{- end }}
  // {{- template "ClientAllocationComment" . }}
  ::fidl::Result {{ .Name }}({{ RenderCalleeParams .RequestArgs }});

    {{- /* Caller-allocate flavor */}}
    {{- if .RequestArgs }}
{{ "" }}
      {{- .Docs }}
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

  std::optional<::fidl::UnbindInfo> DispatchEvent(fidl::IncomingMessage& msg) override;

  std::shared_ptr<{{ .WireAsyncEventHandler }}> event_handler_;
};
{{- EndifFuchsia -}}
{{- end }}

{{- define "ProtocolClientImplDefinition" }}
{{ EnsureNamespace ""}}
{{- IfdefFuchsia -}}
std::optional<::fidl::UnbindInfo>
{{ .WireClientImpl.NoLeading }}::DispatchEvent(fidl::IncomingMessage& msg) {
  {{- if .Events }}
  if (event_handler_ != nullptr) {
    fidl_message_header_t* hdr = msg.header();
    switch (hdr->ordinal) {
    {{- range .Events }}
      case {{ .OrdinalName }}:
      {
        ::fidl::DecodedMessage<{{ .WireResponse }}> decoded{std::move(msg)};
        if (!decoded.ok()) {
          return ::fidl::UnbindInfo{::fidl::UnbindInfo::kDecodeError, decoded.status()};
        }
        event_handler_->{{ .Name }}(decoded.PrimaryObject());
        return std::nullopt;
      }
    {{- end }}
      default:
        break;
    }
  }
  {{- end }}
  return ::fidl::UnbindInfo{::fidl::UnbindInfo::kUnexpectedMessage, ZX_ERR_NOT_SUPPORTED};
}
{{- EndifFuchsia -}}
{{- end }}
`
