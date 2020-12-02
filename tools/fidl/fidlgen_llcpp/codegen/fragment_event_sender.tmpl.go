// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentEventSenderTmpl = `
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
{{ "" }}
  {{- range .Events }}
  zx_status_t {{ .Name }}({{ template "Params" .Response }}) const {
    return Send{{ .Name }}Event(
        ::zx::unowned_channel(server_end_) {{- if .Response }}, {{ end -}}
        {{ template "SyncClientMoveParams" .Response }});
  }

    {{- if .Response }}
{{ "" }}
  zx_status_t {{ .Name }}(::fidl::BufferSpan _buffer,
                          {{ template "Params" .Response }}) const {
    return Send{{ .Name }}Event(
        ::zx::unowned_channel(server_end_), std::move(_buffer),
        {{ template "SyncClientMoveParams" .Response }});
  }
    {{- end }}
{{ "" }}
  {{- end }}
 private:
  ::zx::channel server_end_;
};

class {{ .Name }}::WeakEventSender {
 public:
  {{- range .Events }}
  zx_status_t {{ .Name }}({{ template "Params" .Response }}) const {
    if (auto _binding = binding_.lock()) {
      return Send{{ .Name }}Event(_binding->channel() {{- if .Response }}, {{ end -}} {{ template "SyncClientMoveParams" .Response }});
    }
    return ZX_ERR_CANCELED;
  }

    {{- if .Response }}
{{ "" }}
  zx_status_t {{ .Name }}(::fidl::BufferSpan _buffer,
                          {{ template "Params" .Response }}) const {
    if (auto _binding = binding_.lock()) {
      return Send{{ .Name }}Event(_binding->channel(), std::move(_buffer), {{ template "SyncClientMoveParams" .Response }});
    }
    return ZX_ERR_CANCELED;
  }
    {{- end }}
{{ "" }}
  {{- end }}
 private:
  friend class ::fidl::ServerBindingRef<{{ .Name }}>;

  explicit WeakEventSender(std::weak_ptr<::fidl::internal::AsyncServerBinding> binding)
      : binding_(std::move(binding)) {}

  std::weak_ptr<::fidl::internal::AsyncServerBinding> binding_;
};
{{- end }}
`
