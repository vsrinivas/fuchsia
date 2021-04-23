// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

// fragmentProtocolInterfaceTmpl contains the definition for
// fidl::WireInterface<Protocol> and fidl::WireRawChannelInterface<Protocol>.
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
  ::fidl::DispatchResult dispatch_message(fidl_incoming_msg_t* msg,
                                          ::fidl::Transaction* txn) final;
};

// Pure-virtual interface to be implemented by a server.
// This interface uses typed channels (i.e. |fidl::ClientEnd<SomeProtocol>|
// and |fidl::ServerEnd<SomeProtocol>|).
template<>
class {{ .WireInterface }} : public ::fidl::internal::IncomingMessageDispatcher {
  public:
  WireInterface() = default;
  virtual ~WireInterface() = default;

  // The marker protocol type within which this |{{ .WireInterface.Self }}| class is defined.
  using _EnclosingProtocol = {{ . }};

{{ "" }}
  {{- range .Methods }}
    {{- if .HasRequest }}
{{ "" }}
      {{ if .HasResponse }}
  using {{ .Name }}CompleterBase = {{ .WireCompleterBase }};
      {{ end }}
  using {{ .Name }}Completer = {{ .WireCompleter }};

  {{- .Docs }}
  virtual void {{ .Name }}(
      {{- RenderParams .RequestArgs (printf "%s::Sync& _completer" .WireCompleter.Self)  }})
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
  ::fidl::DispatchResult dispatch_message(fidl_incoming_msg_t* msg,
                                          ::fidl::Transaction* txn) final;
};

{{ "" }}
{{- if .ShouldEmitTypedChannelCascadingInheritance }}
// Pure-virtual interface to be implemented by a server.
// Implementing this interface is discouraged since it uses raw |zx::channel|s
// instead of |fidl::ClientEnd| and |fidl::ServerEnd|. Consider implementing
// |{{ .WireInterface }}| instead.
// TODO(fxbug.dev/65212): Remove this interface after all users have
// migrated to the typed channels API.
template<>
  class FIDL_DEPRECATED_USE_TYPED_CHANNELS {{ .WireRawChannelInterface }} : public {{ .WireInterface }} {
   public:
    WireRawChannelInterface() = default;
    virtual ~WireRawChannelInterface() = default;

    // The marker protocol type within which this |{{ .WireRawChannelInterface.Self }}| class is defined.
    using {{ .WireInterface }}::_EnclosingProtocol;

    {{- range .ClientMethods }}
    using {{ .Name }}Completer = {{ .WireCompleter }};

{{ "" }}
      {{- if .ShouldEmitTypedChannelCascadingInheritance }}
    virtual void {{ .Name }}(
            {{- RenderParams .RequestArgs (printf "%s::Sync& _completer" .WireCompleter) }}) final {
          {{ .Name }}({{ RenderParamsMoveNamesNoTypedChannels .RequestArgs "_completer" }});
    }

    // TODO(fxbug.dev/65212): Overriding this method is discouraged since it
    // uses raw channels instead of |fidl::ClientEnd| and |fidl::ServerEnd|.
    // Please move to overriding the typed channel overload above instead.
    virtual void {{ .Name }}(
      {{- RenderParamsNoTypedChannels .RequestArgs (printf "%s::Sync& _completer" .WireCompleter) }})
        {{- if .Transitional -}}
          { _completer.Close(ZX_ERR_NOT_SUPPORTED); }
        {{- else -}}
          = 0;
        {{- end }}
{{ "" }}
      {{- end }}
    {{- end }}
  };
  {{- end }}
{{- end }}
`
