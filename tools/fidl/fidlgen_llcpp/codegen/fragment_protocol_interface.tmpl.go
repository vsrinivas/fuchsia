// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

// fragmentProtocolInterfaceTmpl contains the definition for fidl::WireServer<Protocol>.
const fragmentProtocolInterfaceTmpl = `
{{- define "ProtocolInterfaceDeclaration" }}
{{ "" }}
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
      {{- if .HasResponse }}
        {{- template "MethodCompleterBaseDeclaration" . }}
  using {{ .WireCompleter.Self }} = ::fidl::Completer<{{ .WireCompleterBase.Self }}>;
    {{- else }}
  using {{ .WireCompleter.Self }} = ::fidl::Completer<>;
    {{- end }}
  class {{ .WireRequestView.Self }} {
   public:
    {{ .WireRequestView.Self }}({{ .WireRequest }}* request) : request_(request) {}
    {{ .WireRequest }}* operator->() const { return request_; }

   private:
    {{ .WireRequest }}* request_;
  };

  {{ .Docs }}
  virtual void {{ .Name }}(
      {{ .WireRequestView.Self }} request, {{ .WireCompleter.Self }}::Sync& _completer)
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
  ::fidl::DispatchResult dispatch_message(::fidl::IncomingMessage&& msg,
                                          ::fidl::Transaction* txn) final;
};
{{- end }}
`
