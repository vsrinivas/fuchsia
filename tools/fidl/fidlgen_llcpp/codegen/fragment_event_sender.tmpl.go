// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentEventSenderTmpl = `
{{- define "SendEventManagedMethodSignature" -}}
{{ .Name }}({{ .ResponseArgs | Params }}) const
{{- end }}

{{- define "SendEventCallerAllocateMethodSignature" -}}
{{ .Name }}(::fidl::BufferSpan _buffer, {{ .ResponseArgs | Params }}) const
{{- end }}

{{- define "EventSenderDeclaration" }}
{{ EnsureNamespace "::" }}
{{- IfdefFuchsia -}}
// |EventSender| owns a server endpoint of a channel speaking
// the {{ .Name }} protocol, and can send events in that protocol.
template<>
class {{ .WireEventSender }} {
 public:
  // Constructs an event sender with an invalid channel.
  WireEventSender() = default;

  explicit WireEventSender(::fidl::ServerEnd<{{ . }}> server_end)
      : server_end_(std::move(server_end)) {}

  // The underlying server channel endpoint, which may be replaced at run-time.
  const ::fidl::ServerEnd<{{ . }}>& server_end() const { return server_end_; }
  ::fidl::ServerEnd<{{ . }}>& server_end() { return server_end_; }

  const ::zx::channel& channel() const { return server_end_.channel(); }
  ::zx::channel& channel() { return server_end_.channel(); }

  // Whether the underlying channel is valid.
  bool is_valid() const { return server_end_.is_valid(); }
{{ "" }}
  {{- /* Events have no "request" part of the call; they are unsolicited. */}}
  {{- range .Events }}
    {{- range .DocComments }}
  //{{ . }}
    {{- end }}
  zx_status_t {{ template "SendEventManagedMethodSignature" . }};

    {{- if .ResponseArgs }}
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
  ::fidl::ServerEnd<{{ . }}> server_end_;
};

template<>
class {{ .WireWeakEventSender }} {
{{- $protocol := . }}
 public:
  {{- range .Events }}
    {{- range .DocComments }}
  //{{ . }}
    {{- end }}
  zx_status_t {{ template "SendEventManagedMethodSignature" . }} {
    if (auto _binding = binding_.lock()) {
      return _binding->event_sender().{{ .Name }}({{ .ResponseArgs | ParamMoveNames }});
    }
    return ZX_ERR_CANCELED;
  }

    {{- if .ResponseArgs }}
{{ "" }}
    {{- range .DocComments }}
  //{{ . }}
    {{- end }}
  // Caller provides the backing storage for FIDL message via response buffers.
  zx_status_t {{ template "SendEventCallerAllocateMethodSignature" . }} {
    if (auto _binding = binding_.lock()) {
      return _binding->event_sender().{{ .Name }}(std::move(_buffer), {{ .ResponseArgs | ParamMoveNames }});
    }
    return ZX_ERR_CANCELED;
  }
    {{- end }}
{{ "" }}
  {{- end }}
 private:
  friend class ::fidl::ServerBindingRef<{{ . }}>;

  explicit WireWeakEventSender(std::weak_ptr<::fidl::internal::AsyncServerBinding<{{ . }}>> binding)
      : binding_(std::move(binding)) {}

  std::weak_ptr<::fidl::internal::AsyncServerBinding<{{ . }}>> binding_;
};
{{- EndifFuchsia -}}
{{- end }}

{{- define "EventSenderDefinition" }}
{{ EnsureNamespace "" }}
{{- IfdefFuchsia -}}
  {{- range .Events }}
    {{- /* Managed */}}
zx_status_t {{ $.WireEventSender.NoLeading }}::
{{- template "SendEventManagedMethodSignature" . }} {
  ::fidl::OwnedEncodedMessage<{{ .WireResponse }}> _response{
      {{- .ResponseArgs | ParamNames -}}
  };
  _response.Write(server_end_);
  return _response.status();
}
    {{- /* Caller-allocated */}}
    {{- if .ResponseArgs }}
{{ "" }}
zx_status_t {{ $.WireEventSender.NoLeading }}::
{{- template "SendEventCallerAllocateMethodSignature" . }} {
  ::fidl::UnownedEncodedMessage<{{ .WireResponse }}> _response(
      _buffer.data, _buffer.capacity
      {{- .ResponseArgs | CommaParamNames -}}
  );
  _response.Write(server_end_);
  return _response.status();
}
    {{- end }}
{{ "" }}
  {{- end }}
{{- EndifFuchsia -}}
{{- end }}
`
