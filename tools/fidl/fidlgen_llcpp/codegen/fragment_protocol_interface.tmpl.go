// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

// fragmentProtocolInterfaceTmpl contains the definition for fidl::WireServer<Protocol>.
const fragmentProtocolInterfaceTmpl = `
{{- define "Protocol:Interface:Header" }}
{{- range .ClientMethods }}
  {{- template "Method:CompleterBase:Header" . }}
{{- end }}

// Pure-virtual interface to be implemented by a server.
// This interface uses typed channels (i.e. |fidl::ClientEnd<SomeProtocol>|
// and |fidl::ServerEnd<SomeProtocol>|).
template<>
class {{ .WireServer }} : public ::fidl::internal::IncomingMessageDispatcher {
  public:
  {{ .WireServer.Self }}() = default;
  virtual ~{{ .WireServer.Self }}() = default;

  // The FIDL protocol type that is implemented by this server.
  using _EnclosingProtocol = {{ . }};

{{ "" }}
  {{- range .Methods }}
    {{- if .HasRequest }}
    using {{ .WireCompleterAlias.Self }} = {{ .WireCompleter }};
    using {{ .WireRequestViewAlias.Self }} = {{ .WireRequestView }};

  {{ .Docs }}
  virtual void {{ .Name }}(
    {{ .WireRequestViewArg }} request, {{ .WireCompleterArg }}& _completer)
      {{- if .Transitional -}}
        { _completer.Close(ZX_ERR_NOT_SUPPORTED); }
      {{- else -}}
        = 0;
      {{- end }}
{{ "" }}
    {{- end }}
  {{- end }}

  private:
  {{- /* Note that this implementation is snake_case to avoid name conflicts. */}}
  void dispatch_message(::fidl::IncomingMessage&& msg, ::fidl::Transaction* txn) final;
};
{{- end }}
`
