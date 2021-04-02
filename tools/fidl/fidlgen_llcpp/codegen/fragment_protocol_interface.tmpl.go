// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

// fragmentProtocolInterfaceTmpl contains the definition for
// fidl::WireInterface<Protocol> and fidl::WireRawChannelInterface<Protocol>
const fragmentProtocolInterfaceTmpl = `
{{- define "ProtocolInterfaceDeclaration" }}
{{ "" }}
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
      {{- if .HasResponse }}
  class {{ .WireCompleterBase.Self }} : public ::fidl::CompleterBase {
    public:
    // In the following methods, the return value indicates internal errors during
    // the reply, such as encoding or writing to the transport.
    // Note that any error will automatically lead to the destruction of the binding,
    // after which the |on_unbound| callback will be triggered with a detailed reason.
    //
    // See //zircon/system/ulib/fidl/include/lib/fidl/llcpp/server.h.
    //
    // Because the reply status is identical to the unbinding status, it can be safely ignored.
    ::fidl::Result {{ template "ReplyManagedMethodSignature" . }};
        {{- if .Result }}
    ::fidl::Result {{ template "ReplyManagedResultSuccessMethodSignature" . }};
    ::fidl::Result {{ template "ReplyManagedResultErrorMethodSignature" . }};
        {{- end }}
        {{- if .ResponseArgs }}
    ::fidl::Result {{ template "ReplyCallerAllocateMethodSignature" . }};
          {{- if .Result }}
    ::fidl::Result {{ template "ReplyCallerAllocateResultSuccessMethodSignature" . }};
          {{- end }}
        {{- end }}

    protected:
    using ::fidl::CompleterBase::CompleterBase;
  };

  using {{ .WireCompleter.Self }} = ::fidl::Completer<{{ .WireCompleterBase.Self }}>;
      {{- else }}
  using {{ .WireCompleter.Self }} = ::fidl::Completer<>;
      {{- end }}

{{ "" }}
  {{- .Docs }}
  virtual void {{ .Name }}(
      {{- .RequestArgs | Params }}{{ if .RequestArgs }}, {{ end -}}
      {{- if .Transitional -}}
        {{ .WireCompleter.Self }}::Sync& _completer) { _completer.Close(ZX_ERR_NOT_SUPPORTED); }
      {{- else -}}
        {{ .WireCompleter.Self }}::Sync& _completer) = 0;
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
    using {{ .WireCompleter }};

{{ "" }}
      {{- if .ShouldEmitTypedChannelCascadingInheritance }}
    virtual void {{ .Name }}(
        {{- .RequestArgs | Params }}{{ if .RequestArgs }}, {{ end -}}
        {{ .WireCompleter }}::Sync& _completer) final {
      {{ .Name }}({{ template "ForwardMessageParamsUnwrapTypedChannels" .RequestArgs }}
        {{- if .RequestArgs }}, {{ end -}} _completer);
    }

    // TODO(fxbug.dev/65212): Overriding this method is discouraged since it
    // uses raw channels instead of |fidl::ClientEnd| and |fidl::ServerEnd|.
    // Please move to overriding the typed channel overload above instead.
    virtual void {{ .Name }}(
      {{- .RequestArgs | ParamsNoTypedChannels }}{{ if .RequestArgs }}, {{ end -}}
        {{- if .Transitional -}}
          {{ .WireCompleter }}::Sync& _completer) { _completer.Close(ZX_ERR_NOT_SUPPORTED); }
        {{- else -}}
          {{ .WireCompleter }}::Sync& _completer) = 0;
        {{- end }}
{{ "" }}
      {{- end }}
    {{- end }}
  };
  {{- end }}
{{- end }}
`
