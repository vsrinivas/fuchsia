// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentEventSenderTmpl = `
{{- define "EventSenderDeclaration" }}
{{ EnsureNamespace "" }}
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
    {{- .Docs }}
    zx_status_t {{ .Name }}({{ RenderParams .ResponseArgs }}) const;

    {{- if .ResponseArgs }}
{{ "" }}
    {{- .Docs }}
    // Caller provides the backing storage for FIDL message via response buffers.
    zx_status_t {{ .Name }}({{ RenderParams "::fidl::BufferSpan _buffer" .ResponseArgs }}) const;
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
    {{- .Docs }}
    zx_status_t {{ .Name }}({{ RenderParams .ResponseArgs }}) const {
      if (auto _binding = binding_.lock()) {
        return _binding->event_sender().{{ .Name }}({{ RenderForwardParams .ResponseArgs }});
      }
      return ZX_ERR_CANCELED;
    }

    {{- if .ResponseArgs }}
{{ "" }}
    {{- .Docs }}
  // Caller provides the backing storage for FIDL message via response buffers.
  zx_status_t {{ .Name }}({{ RenderParams "::fidl::BufferSpan _buffer" .ResponseArgs }}) const {
    if (auto _binding = binding_.lock()) {
      return _binding->event_sender().{{ .Name }}({{ RenderForwardParams "std::move(_buffer)" .ResponseArgs }});
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
zx_status_t {{ $.WireEventSender.NoLeading }}::{{ .Name }}({{ RenderParams .ResponseArgs }}) const {
  FIDL_INTERNAL_DISABLE_AUTO_VAR_INIT
  ::fidl::OwnedEncodedMessage<{{ .WireResponse }}> _response{
      {{- RenderForwardParams "::fidl::internal::AllowUnownedInputRef{}" .ResponseArgs -}}
  };
  _response.Write(server_end_);
  return _response.status();
}
    {{- /* Caller-allocated */}}
    {{- if .ResponseArgs }}
{{ "" }}
zx_status_t {{ $.WireEventSender.NoLeading }}::{{ .Name }}({{ RenderParams "::fidl::BufferSpan _buffer" .ResponseArgs }}) const {
  ::fidl::UnownedEncodedMessage<{{ .WireResponse }}> _response(
      {{- RenderForwardParams "_buffer.data" "_buffer.capacity" .ResponseArgs }});
  _response.Write(server_end_);
  return _response.status();
}
    {{- end }}
{{ "" }}
  {{- end }}
{{- EndifFuchsia -}}
{{- end }}
`
