// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fragments

const Interface = `
{{- define "InterfaceForwardDeclaration" }}
class {{ .Name }};
{{- end }}

{{- define "RequestCodingTable" -}}
{{- if .LLProps.EncodeRequest -}}
&{{ .RequestTypeName }}
{{- else -}}
nullptr
{{- end }}
{{- end }}

{{- define "ResponseCodingTable" -}}
{{- if .LLProps.DecodeResponse -}}
&{{ .ResponseTypeName }}
{{- else -}}
nullptr
{{- end }}
{{- end }}

{{- define "ForwardParams" -}}
  {{- range $index, $param := . -}}
    {{- if $index }}, {{ end -}} std::move({{ $param.Name }})
  {{- end -}}
{{- end }}

{{- define "InterfaceDeclaration" }}
{{- $interface := . }}
{{ "" }}
  {{- range .Methods }}
    {{- if .LLProps.EncodeRequest }}
extern "C" const fidl_type_t {{ .RequestTypeName }};
    {{- end }}
    {{- if .LLProps.DecodeResponse }}
extern "C" const fidl_type_t {{ .ResponseTypeName }};
    {{- end }}
  {{- end }}

  {{- /* Trailing line feed after encoding tables. */}}
  {{- if MethodsHaveReqOrResp .Methods }}
{{ "" }}
  {{- end }}
  {{- /* End trailing line feed after encoding tables. */}}

{{- range .DocComments }}
//{{ . }}
{{- end }}
class {{ .Name }} final {
 public:
{{ "" }}
  {{- range .Methods }}

    {{- if .HasResponse }}
      {{- if .Response }}
  struct {{ .Name }}Response final {
    FIDL_ALIGNDECL
        {{- /* Add underscore to prevent name collision */}}
    fidl_message_header_t _hdr;
        {{- range $index, $param := .Response }}
    {{ $param.Type.LLDecl }} {{ $param.Name }};
        {{- end }}

    static constexpr const fidl_type_t* Type = {{ template "ResponseCodingTable" . }};
    static constexpr uint32_t MaxNumHandles = {{ .ResponseMaxHandles }};
    static constexpr uint32_t PrimarySize = {{ .ResponseSize }};
    static constexpr uint32_t MaxOutOfLine = {{ .ResponseMaxOutOfLine }};
  };
      {{- else }}
  using {{ .Name }}Response = ::fidl::AnyZeroArgMessage;
      {{- end }}
    {{- end }}

    {{- if .HasRequest }}
      {{- if .Request }}
  struct {{ .Name }}Request final {
    FIDL_ALIGNDECL
        {{- /* Add underscore to prevent name collision */}}
    fidl_message_header_t _hdr;
        {{- range $index, $param := .Request }}
    {{ $param.Type.LLDecl }} {{ $param.Name }};
        {{- end }}

    static constexpr const fidl_type_t* Type = {{ template "RequestCodingTable" . }};
    static constexpr uint32_t MaxNumHandles = {{ .RequestMaxHandles }};
    static constexpr uint32_t PrimarySize = {{ .RequestSize }};
    static constexpr uint32_t MaxOutOfLine = {{ .RequestMaxOutOfLine }};

        {{- if and .HasResponse .Response }}
    using ResponseType = {{ .Name }}Response;
        {{- end }}
  };
{{ "" }}
      {{- else }}
  using {{ .Name }}Request = ::fidl::AnyZeroArgMessage;
{{ "" }}
      {{- end }}
    {{- end }}

  {{- end }}

  {{- if .HasEvents }}
{{ "" }}
  struct EventHandlers {
    {{- range FilterMethodsWithReqs .Methods -}}
      {{- range .DocComments }}
    //{{ . }}
      {{- end }}
    fit::function<zx_status_t {{- template "SyncEventHandlerIndividualMethodSignature" . }}> {{ .NameInLowerSnakeCase }};
{{ "" }}
    {{- end }}
    // Fallback handler when an unknown ordinal is received.
    // Caller may put custom error handling logic here.
    fit::function<zx_status_t()> unknown;
  };
  {{- end }}

  class SyncClient final {
   public:
    SyncClient(::zx::channel channel) : channel_(std::move(channel)) {}

    ~SyncClient() {}
{{ "" }}
    {{- /* Client-calling functions do not apply to events. */}}
    {{- range FilterMethodsWithoutReqs .Methods -}}
      {{- if .LLProps.CBindingCompatible }}
        {{- range .DocComments }}
    //{{ . }}
        {{- end }}
    zx_status_t {{ template "SyncRequestCFlavorMethodSignature" . }};
      {{- end }}
{{ "" }}
      {{- if or .Request .Response }}
        {{- range .DocComments }}
    //{{ . }}
        {{- end }}
    // Caller provides the backing storage for FIDL message via request and response buffers.
    {{- if .HasResponse }}
    // The lifetime of handles in the response, unless moved, is tied to the returned RAII object.
    {{- end }}
    {{ if .HasResponse -}}
    ::fidl::DecodeResult<{{ .Name }}Response>
    {{- else -}}
    zx_status_t
    {{- end }} {{ template "SyncRequestCallerAllocateMethodSignature" . }};
{{ "" }}
      {{- end }}
      {{- if or .Request .Response }}
        {{- range .DocComments }}
    //{{ . }}
        {{- end }}
    // Messages are encoded and decoded in-place.
    {{ if .Response }}::fidl::DecodeResult<{{ .Name }}Response>{{ else }}zx_status_t{{ end }} {{ template "SyncRequestInPlaceMethodSignature" . }};
{{ "" }}
      {{- end }}
    {{- end }}
    {{- if .HasEvents }}
    // Handle all possible events defined in this protocol.
    // Blocks to consume exactly one message from the channel, then call the corresponding handler
    // defined in |EventHandlers|. The return status of the handler function is folded with any
    // transport-level errors and returned.
    zx_status_t HandleEvents(EventHandlers handlers);
    {{- end }}
   private:
    ::zx::channel channel_;
  };

  // Methods to make a sync FIDL call directly on an unowned channel, avoiding setting up a client.
  class Call final {
   public:
{{ "" }}
    {{- /* Client-calling functions do not apply to events. */}}
    {{- range FilterMethodsWithoutReqs .Methods -}}
      {{- if .LLProps.CBindingCompatible }}
        {{- range .DocComments }}
    //{{ . }}
        {{- end }}
    static zx_status_t {{ template "StaticCallSyncRequestCFlavorMethodSignature" . }};
      {{- end }}
{{ "" }}
      {{- if or .Request .Response }}
        {{- range .DocComments }}
    //{{ . }}
        {{- end }}
    // Caller provides the backing storage for FIDL message via request and response buffers.
    {{- if .HasResponse }}
    // The lifetime of handles in the response, unless moved, is tied to the returned RAII object.
    {{- end }}
    static {{ if .HasResponse -}}
    ::fidl::DecodeResult<{{ .Name }}Response>
    {{- else -}}
    zx_status_t
    {{- end }} {{ template "StaticCallSyncRequestCallerAllocateMethodSignature" . }};
{{ "" }}
      {{- end }}
      {{- if or .Request .Response }}
        {{- range .DocComments }}
    //{{ . }}
        {{- end }}
    // Messages are encoded and decoded in-place.
    static {{ if .Response }}::fidl::DecodeResult<{{ .Name }}Response>{{ else }}zx_status_t{{ end }} {{ template "StaticCallSyncRequestInPlaceMethodSignature" . }};
{{ "" }}
      {{- end }}
    {{- end }}
    {{- if .HasEvents }}
    // Handle all possible events defined in this protocol.
    // Blocks to consume exactly one message from the channel, then call the corresponding handler
    // defined in |EventHandlers|. The return status of the handler function is folded with any
    // transport-level errors and returned.
    static zx_status_t HandleEvents(zx::unowned_channel client_end, EventHandlers handlers);
    {{- end }}
  };

  {{- if .Methods }}
{{ "" }}
  // Pure-virtual interface to be implemented by a server.
  class Interface {
   public:
    Interface() = default;
    virtual ~Interface() = default;
    using _Outer = {{ $interface.Name }};
    using _Base = ::fidl::CompleterBase;
{{ "" }}
    {{- range .Methods }}
      {{- if .HasRequest }}
        {{- if .HasResponse }}
    class {{ .Name }}CompleterBase : public _Base {
     public:
      void {{ template "ReplyCFlavorMethodSignature" . }};
          {{- if .Response }}
      void {{ template "ReplyCallerAllocateMethodSignature" . }};
      void {{ template "ReplyInPlaceMethodSignature" . }};
          {{- end }}

     protected:
      using ::fidl::CompleterBase::CompleterBase;
    };

    using {{ .Name }}Completer = ::fidl::Completer<{{ .Name }}CompleterBase>;
        {{- else }}
    using {{ .Name }}Completer = ::fidl::Completer<>;
        {{- end }}

    virtual void {{ .Name }}(
        {{- template "Params" .Request }}{{ if .Request }}, {{ end -}}
        {{ .Name }}Completer::Sync _completer) = 0;
{{ "" }}
      {{- end }}
    {{- end }}
  };

  // Attempts to dispatch the incoming message to a handler function in the server implementation.
  // If there is no matching handler, it returns false, leaving the message and transaction intact.
  // In all other cases, it consumes the message and returns true.
  // It is possible to chain multiple TryDispatch functions in this manner.
  static bool TryDispatch{{ template "SyncServerDispatchMethodSignature" }};

  // Dispatches the incoming message to one of the handlers functions in the interface.
  // If there is no matching handler, it closes all the handles in |msg| and closes the channel with
  // a |ZX_ERR_NOT_SUPPORTED| epitaph, before returning false. The message should then be discarded.
  static bool Dispatch{{ template "SyncServerDispatchMethodSignature" }};

  // Same as |Dispatch|, but takes a |void*| instead of |Interface*|. Only used with |fidl::Bind|
  // to reduce template expansion.
  // Do not call this method manually. Use |Dispatch| instead.
  static bool TypeErasedDispatch(void* impl, fidl_msg_t* msg, ::fidl::Transaction* txn) {
    return Dispatch(static_cast<Interface*>(impl), msg, txn);
  }
  {{- end }}

  {{- /* Events */}}
  {{- if .Methods }}
{{ "" }}
    {{- /* Events have no "request" part of the call; they are unsolicited. */}}
    {{- range FilterMethodsWithReqs .Methods | FilterMethodsWithoutResps -}}
{{ "" }}
      {{- range .DocComments }}
  //{{ . }}
      {{- end }}
  static zx_status_t {{ template "SendEventCFlavorMethodSignature" . }};
      {{- if .Response }}
{{ "" }}
        {{- range .DocComments }}
  //{{ . }}
        {{- end }}
  // Caller provides the backing storage for FIDL message via response buffers.
  static zx_status_t {{ template "SendEventCallerAllocateMethodSignature" . }};
      {{- end }}
      {{- if .Response }}
{{ "" }}
        {{- range .DocComments }}
  //{{ . }}
        {{- end }}
  // Messages are encoded in-place.
  static zx_status_t {{ template "SendEventInPlaceMethodSignature" . }};
      {{- end }}
{{ "" }}
    {{- end }}
  {{- end }}
};
{{- end }}

{{- define "InterfaceTraits" -}}
{{ $interface := . -}}
{{ range .Methods -}}
{{ $method := . -}}
{{- if and .HasRequest .Request }}

template <>
struct IsFidlType<{{ $interface.Namespace }}::{{ $interface.Name }}::{{ .Name }}Request> : public std::true_type {};
template <>
struct IsFidlMessage<{{ $interface.Namespace }}::{{ $interface.Name }}::{{ .Name }}Request> : public std::true_type {};
static_assert(sizeof({{ $interface.Namespace }}::{{ $interface.Name }}::{{ .Name }}Request)
    == {{ $interface.Namespace }}::{{ $interface.Name }}::{{ .Name }}Request::PrimarySize);
{{- range $index, $param := .Request }}
static_assert(offsetof({{ $interface.Namespace }}::{{ $interface.Name }}::{{ $method.Name }}Request, {{ $param.Name }}) == {{ $param.Offset }});
{{- end }}
{{- end }}
{{- if and .HasResponse .Response }}

template <>
struct IsFidlType<{{ $interface.Namespace }}::{{ $interface.Name }}::{{ .Name }}Response> : public std::true_type {};
template <>
struct IsFidlMessage<{{ $interface.Namespace }}::{{ $interface.Name }}::{{ .Name }}Response> : public std::true_type {};
static_assert(sizeof({{ $interface.Namespace }}::{{ $interface.Name }}::{{ .Name }}Response)
    == {{ $interface.Namespace }}::{{ $interface.Name }}::{{ .Name }}Response::PrimarySize);
{{- range $index, $param := .Response }}
static_assert(offsetof({{ $interface.Namespace }}::{{ $interface.Name }}::{{ $method.Name }}Response, {{ $param.Name }}) == {{ $param.Offset }});
{{- end }}
{{- end }}
{{- end }}
{{- end }}

{{- define "FillRequestStructMembers" }}
{{- range $param := . }}
  _request.{{ $param.Name }} = std::move({{ $param.Name }});
{{- end }}
{{- end }}

{{- define "FillResponseStructMembers" }}
{{- range $param := . }}
  _response.{{ $param.Name }} = std::move({{ $param.Name }});
{{- end }}
{{- end }}

{{- define "ReturnResponseStructMembers" }}
{{- range $param := . }}
  *out_{{ $param.Name }} = std::move(_response.{{ $param.Name }});
{{- end }}
{{- end }}

{{- define "InterfaceDefinition" }}

namespace {
{{ $interface := . -}}

{{- range .Methods }}
[[maybe_unused]]
constexpr uint32_t {{ .OrdinalName }} = {{ .Ordinal }}u;
  {{- if .LLProps.EncodeRequest }}
extern "C" const fidl_type_t {{ .RequestTypeName }};
  {{- end }}
  {{- if .LLProps.DecodeResponse }}
extern "C" const fidl_type_t {{ .ResponseTypeName }};
  {{- end }}
{{- end }}

}  // namespace

{{- /* Client-calling functions do not apply to events. */}}
{{- range FilterMethodsWithoutReqs .Methods -}}
  {{- if .LLProps.CBindingCompatible }}
{{ "" }}
    {{- template "SyncRequestCFlavorMethodDefinition" . }}
{{ "" }}
    {{- template "StaticCallSyncRequestCFlavorMethodDefinition" . }}
  {{- end }}
  {{- if or .Request .Response }}
{{ "" }}
    {{- template "SyncRequestCallerAllocateMethodDefinition" . }}
{{ "" }}
    {{- template "StaticCallSyncRequestCallerAllocateMethodDefinition" . }}
  {{- end }}
  {{- if or .Request .Response }}
{{ "" }}
    {{- template "SyncRequestInPlaceMethodDefinition" . }}
{{ "" }}
    {{- template "StaticCallSyncRequestInPlaceMethodDefinition" . }}
  {{- end }}
{{ "" }}
{{- end }}

{{- if .HasEvents }}
  {{- template "SyncEventHandlerMethodDefinition" . }}
{{ "" }}
  {{- template "StaticCallSyncEventHandlerMethodDefinition" . }}
{{- end }}

{{- /* Server implementation */}}
{{- if .Methods }}
{{ template "SyncServerTryDispatchMethodDefinition" . }}
{{ template "SyncServerDispatchMethodDefinition" . }}
{{- end }}

{{- if .Methods }}
{{ "" }}
  {{- range FilterMethodsWithoutResps .Methods -}}
    {{- if not .HasRequest }}
{{ "" }}
      {{- template "SendEventCFlavorMethodDefinition" . }}
      {{- if .Response }}
{{ "" }}
        {{- template "SendEventCallerAllocateMethodDefinition" . }}
      {{- end }}
      {{- if .Response }}
{{ "" }}
        {{- template "SendEventInPlaceMethodDefinition" . }}
      {{- end }}
{{ "" }}
    {{- else }}
{{ "" }}
      {{- template "ReplyCFlavorMethodDefinition" . }}
      {{- if .Response }}
{{ "" }}
        {{- template "ReplyCallerAllocateMethodDefinition" . }}
      {{- end }}
      {{- if .Response }}
{{ "" }}
        {{- template "ReplyInPlaceMethodDefinition" . }}
      {{- end }}
{{ "" }}
    {{- end }}
  {{- end }}
{{- end }}

{{- end }}
`
