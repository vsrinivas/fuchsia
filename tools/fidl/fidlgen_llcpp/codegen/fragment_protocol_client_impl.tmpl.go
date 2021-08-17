// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentProtocolClientImplTmpl = `
{{- define "ProtocolClientImplDeclaration" }}
{{- IfdefFuchsia -}}
{{- EnsureNamespace "" }}
template<>
class {{ .WireClientImpl }} final : public ::fidl::internal::ClientBase {
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
    {{ RenderParams .RequestArgs
                    (printf "::fidl::WireClientCallback<%s> _cb" .Marker) }});

  ::fidl::Result {{ .Name }}({{ RenderParams .RequestArgs
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
          {{ RenderParams "::fidl::BufferSpan _request_buffer"
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
  {{ .WireResult }} {{ .Name }}_Sync({{ RenderParams .RequestArgs }});

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
  ::fidl::Result {{ .Name }}({{ RenderParams .RequestArgs }});

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

  {{ .WireClientImpl.Self }}() = default;

 private:
  std::optional<::fidl::UnbindInfo> DispatchEvent(
      ::fidl::IncomingMessage& msg,
      ::fidl::internal::AsyncEventHandler* maybe_event_handler) override;
};
{{- EndifFuchsia -}}
{{- end }}

{{- define "ProtocolClientImplDefinition" }}
{{ EnsureNamespace ""}}
{{- IfdefFuchsia -}}
std::optional<::fidl::UnbindInfo>
{{ .WireClientImpl.NoLeading }}::DispatchEvent(
    fidl::IncomingMessage& msg,
    ::fidl::internal::AsyncEventHandler* maybe_event_handler) {
  {{- if .Events }}
  auto* event_handler = static_cast<{{ .WireAsyncEventHandler }}*>(maybe_event_handler);
  fidl_message_header_t* hdr = msg.header();
  switch (hdr->ordinal) {
  {{- range .Events }}
    case {{ .OrdinalName }}:
    {
      ::fidl::DecodedMessage<{{ .WireResponse }}> decoded{std::move(msg)};
      if (!decoded.ok()) {
        return ::fidl::UnbindInfo{decoded};
      }
      if (event_handler) {
        event_handler->{{ .Name }}(decoded.PrimaryObject());
      }
      return std::nullopt;
    }
  {{- end }}
    default:
      break;
  }
  {{- end }}
  return ::fidl::UnbindInfo::UnknownOrdinal();
}
{{- EndifFuchsia -}}
{{- end }}
`
