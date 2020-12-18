// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentEventSenderTmpl = `
{{- define "SendEventManagedMethodSignature" -}}
{{ .Name }}({{ template "Params" .Response }}) const
{{- end }}

{{- define "SendEventCallerAllocateMethodSignature" -}}
{{ .Name }}(::fidl::BufferSpan _buffer, {{ template "Params" .Response }}) const
{{- end }}

{{- define "EventSenderDeclaration" }}
// |EventSender| owns a server endpoint of a channel speaking
// the {{ .Name }} protocol, and can send events in that protocol.
class {{ .Name }}::EventSender {
 public:
  // Constructs an event sender with an invalid channel.
  EventSender() = default;

  // TODO(fxbug.dev/65212): EventSender should take a ::fidl::ServerEnd.
  explicit EventSender(::zx::channel server_end)
      : server_end_(std::move(server_end)) {}

  // The underlying server channel endpoint, which may be replaced at run-time.
  const ::zx::channel& channel() const { return server_end_; }
  ::zx::channel& channel() { return server_end_; }

  // Whether the underlying channel is valid.
  bool is_valid() const { return server_end_.is_valid(); }
{{ "" }}
  {{- /* Events have no "request" part of the call; they are unsolicited. */}}
  {{- range .Events }}
    {{- range .DocComments }}
  //{{ . }}
    {{- end }}
  zx_status_t {{ template "SendEventManagedMethodSignature" . }};

    {{- if .Response }}
{{ "" }}
    {{- range .DocComments }}
  //{{ . }}
    {{- end }}
  // Caller provides the backing storage for FIDL message via response buffers.
  zx_status_t {{ template "SendEventCallerAllocateMethodSignature" . }};
    {{- end }}
{{ "" }}
  {{- end }}
 private:
  ::zx::channel server_end_;
};

class {{ .Name }}::WeakEventSender {
{{- $protocol := . }}
 public:
  {{- range .Events }}
    {{- range .DocComments }}
  //{{ . }}
    {{- end }}
  zx_status_t {{ template "SendEventManagedMethodSignature" . }} {
    if (auto _binding = binding_.lock()) {
      return _binding->event_sender().{{ .Name }}(
          {{ template "SyncClientMoveParams" .Response }});
    }
    return ZX_ERR_CANCELED;
  }

    {{- if .Response }}
{{ "" }}
    {{- range .DocComments }}
  //{{ . }}
    {{- end }}
  // Caller provides the backing storage for FIDL message via response buffers.
  zx_status_t {{ template "SendEventCallerAllocateMethodSignature" . }} {
    if (auto _binding = binding_.lock()) {
      return _binding->event_sender().{{ .Name }}(
          std::move(_buffer), {{ template "SyncClientMoveParams" .Response }});
    }
    return ZX_ERR_CANCELED;
  }
    {{- end }}
{{ "" }}
  {{- end }}
 private:
  friend class ::fidl::ServerBindingRef<{{ .Name }}>;

  explicit WeakEventSender(std::weak_ptr<::fidl::internal::AsyncServerBinding<{{ .Name }}>> binding)
      : binding_(std::move(binding)) {}

  std::weak_ptr<::fidl::internal::AsyncServerBinding<{{ .Name }}>> binding_;
};
{{- end }}

{{- define "EventSenderDefinition" }}
  {{- range .Events }}
    {{- /* Managed */}}
zx_status_t {{ .LLProps.ProtocolName }}::EventSender::
{{- template "SendEventManagedMethodSignature" . }} {
  {{ .Name }}Response::OwnedEncodedMessage _response{
      {{- template "PassthroughMessageParams" .Response -}}
  };
  _response.Write(server_end_.get());
  return _response.status();
}
    {{- /* Caller-allocated */}}
    {{- if .Response }}
{{ "" }}
zx_status_t {{ .LLProps.ProtocolName }}::EventSender::
{{- template "SendEventCallerAllocateMethodSignature" . }} {
  {{ .Name }}Response::UnownedEncodedMessage _response(_buffer.data, _buffer.capacity
      {{- template "CommaPassthroughMessageParams" .Response -}}
  );
  _response.Write(server_end_.get());
  return _response.status();
}
    {{- end }}
{{ "" }}
  {{- end }}
{{- end }}
`
