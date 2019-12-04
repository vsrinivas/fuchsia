// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fragments

const Interface = `
{{- define "InterfaceForwardDeclaration" }}
class {{ .Name }};
{{- end }}

{{- define "RequestCodingTable" -}}
&{{ .RequestTypeName }}
{{- end }}

{{- define "ResponseCodingTable" -}}
&{{ .ResponseTypeName }}
{{- end }}

{{- define "V1ResponseCodingTable" -}}
&{{ .V1ResponseTypeName }}
{{- end }}

{{- define "V1RequestCodingTable" -}}
&{{ .V1RequestTypeName }}
{{- end }}

{{- define "ForwardParams" -}}
  {{- range $index, $param := . -}}
    {{- if $index }}, {{ end -}} std::move({{ $param.Name }})
  {{- end -}}
{{- end }}

{{- define "ClientAllocationComment" -}}
{{- $context := .LLProps.ClientContext }}
{{- if $context.StackUse }} Allocates {{ $context.StackUse }} bytes of {{ "" }}
{{- if not $context.StackAllocRequest -}} response {{- else -}}
  {{- if not $context.StackAllocResponse -}} request {{- else -}} message {{- end -}}
{{- end }} buffer on the stack. {{- end }}
{{- if and $context.StackAllocRequest $context.StackAllocResponse }} No heap allocation necessary.
{{- else }}
  {{- if not $context.StackAllocRequest }} Request is heap-allocated. {{- end }}
  {{- if not $context.StackAllocResponse }} Response is heap-allocated. {{- end }}
{{- end }}
{{- end }}

{{- define "InterfaceDeclaration" }}
{{- $interface := . }}
{{ "" }}
  {{- range .Methods }}
extern "C" const fidl_type_t {{ .RequestTypeName }};
extern "C" const fidl_type_t {{ .V1RequestTypeName }};
extern "C" const fidl_type_t {{ .ResponseTypeName }};
extern "C" const fidl_type_t {{ .V1ResponseTypeName }};
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
  {{ .Name }}() = delete;
 public:
{{- if .ServiceName }}
  static constexpr char Name[] = {{ .ServiceName }};
{{- end }}
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
    static constexpr const fidl_type_t* AltType = {{ template "V1ResponseCodingTable" . }};
    static constexpr uint32_t MaxNumHandles = {{ .ResponseMaxHandles }};
    static constexpr uint32_t PrimarySize = {{ .ResponseSize }};
    static constexpr uint32_t MaxOutOfLine = {{ .ResponseMaxOutOfLine }};
    static constexpr uint32_t AltPrimarySize = {{ .ResponseSizeV1NoEE }};
    static constexpr uint32_t AltMaxOutOfLine = {{ .ResponseMaxOutOfLineV1NoEE }};
    static constexpr bool HasFlexibleEnvelope = {{ .ResponseFlexible }};
    static constexpr bool ContainsUnion = {{ .ResponseContainsUnion }};
    static constexpr ::fidl::internal::TransactionalMessageKind MessageKind =
        ::fidl::internal::TransactionalMessageKind::kResponse;
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
    static constexpr const fidl_type_t* AltType = {{ template "V1RequestCodingTable" . }};
    static constexpr uint32_t MaxNumHandles = {{ .RequestMaxHandles }};
    static constexpr uint32_t PrimarySize = {{ .RequestSize }};
    static constexpr uint32_t MaxOutOfLine = {{ .RequestMaxOutOfLine }};
    static constexpr uint32_t AltPrimarySize = {{ .RequestSizeV1NoEE }};
    static constexpr uint32_t AltMaxOutOfLine = {{ .RequestMaxOutOfLineV1NoEE }};
    static constexpr bool HasFlexibleEnvelope = {{ .RequestFlexible }};
    static constexpr bool ContainsUnion = {{ .RequestContainsUnion }};
    static constexpr ::fidl::internal::TransactionalMessageKind MessageKind =
        ::fidl::internal::TransactionalMessageKind::kRequest;

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
    fit::callback<zx_status_t {{- template "SyncEventHandlerIndividualMethodSignature" . }}> {{ .NameInLowerSnakeCase }};
{{ "" }}
    {{- end }}
    // Fallback handler when an unknown ordinal is received.
    // Caller may put custom error handling logic here.
    fit::callback<zx_status_t()> unknown;
  };
  {{- end }}

  // Collection of return types of FIDL calls in this interface.
  class ResultOf final {
    ResultOf() = delete;
   private:
    {{- range FilterMethodsWithoutReqs .Methods -}}
    {{- if .HasResponse -}}
{{ "" }}
    template <typename ResponseType>
    {{- end }}
    class {{ .Name }}_Impl final : private ::fidl::internal::{{ if .HasResponse -}} OwnedSyncCallBase<ResponseType> {{- else -}} StatusAndError {{- end }} {
      using Super = ::fidl::internal::{{ if .HasResponse -}} OwnedSyncCallBase<ResponseType> {{- else -}} StatusAndError {{- end }};
     public:
      {{ .Name }}_Impl({{ template "StaticCallSyncRequestManagedMethodArguments" . }});
      ~{{ .Name }}_Impl() = default;
      {{ .Name }}_Impl({{ .Name }}_Impl&& other) = default;
      {{ .Name }}_Impl& operator=({{ .Name }}_Impl&& other) = default;
      using Super::status;
      using Super::error;
      using Super::ok;
      {{- if .HasResponse }}
      using Super::Unwrap;
      using Super::value;
      using Super::operator->;
      using Super::operator*;
      {{- end }}
    };
    {{- end }}

   public:
    {{- range FilterMethodsWithoutReqs .Methods -}}
      {{- if .HasResponse }}
    using {{ .Name }} = {{ .Name }}_Impl<{{ .Name }}Response>;
      {{- else }}
    using {{ .Name }} = {{ .Name }}_Impl;
      {{- end }}
    {{- end }}
  };

  // Collection of return types of FIDL calls in this interface,
  // when the caller-allocate flavor or in-place call is used.
  class UnownedResultOf final {
    UnownedResultOf() = delete;
   private:
    {{- range FilterMethodsWithoutReqs .Methods -}}
    {{- if .HasResponse -}}
{{ "" }}
    template <typename ResponseType>
    {{- end }}
    class {{ .Name }}_Impl final : private ::fidl::internal::{{ if .HasResponse -}} UnownedSyncCallBase<ResponseType> {{- else -}} StatusAndError {{- end }} {
      using Super = ::fidl::internal::{{ if .HasResponse -}} UnownedSyncCallBase<ResponseType> {{- else -}} StatusAndError {{- end }};
     public:
      {{ .Name }}_Impl({{ template "StaticCallSyncRequestCallerAllocateMethodArguments" . }});
      ~{{ .Name }}_Impl() = default;
      {{ .Name }}_Impl({{ .Name }}_Impl&& other) = default;
      {{ .Name }}_Impl& operator=({{ .Name }}_Impl&& other) = default;
      using Super::status;
      using Super::error;
      using Super::ok;
      {{- if .HasResponse }}
      using Super::Unwrap;
      using Super::value;
      using Super::operator->;
      using Super::operator*;
      {{- end }}
    };
    {{- end }}

   public:
    {{- range FilterMethodsWithoutReqs .Methods -}}
      {{- if .HasResponse }}
    using {{ .Name }} = {{ .Name }}_Impl<{{ .Name }}Response>;
      {{- else }}
    using {{ .Name }} = {{ .Name }}_Impl;
      {{- end }}
    {{- end }}
  };

  class SyncClient final {
   public:
    explicit SyncClient(::zx::channel channel) : channel_(std::move(channel)) {}
    ~SyncClient() = default;
    SyncClient(SyncClient&&) = default;
    SyncClient& operator=(SyncClient&&) = default;

    const ::zx::channel& channel() const { return channel_; }

    ::zx::channel* mutable_channel() { return &channel_; }
{{ "" }}
    {{- /* Client-calling functions do not apply to events. */}}
    {{- range FilterMethodsWithoutReqs .Methods -}}
      {{- range .DocComments }}
    //{{ . }}
      {{- end }}
    //{{ template "ClientAllocationComment" . }}
    ResultOf::{{ .Name }} {{ .Name }}({{ template "SyncRequestManagedMethodArguments" . }});
{{ "" }}
      {{- if or .Request .Response }}
        {{- range .DocComments }}
    //{{ . }}
        {{- end }}
    // Caller provides the backing storage for FIDL message via request and response buffers.
    UnownedResultOf::{{ .Name }} {{ .Name }}({{ template "SyncRequestCallerAllocateMethodArguments" . }});
      {{- end }}
{{ "" }}
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
    Call() = delete;
   public:
{{ "" }}
    {{- /* Client-calling functions do not apply to events. */}}
    {{- range FilterMethodsWithoutReqs .Methods -}}
      {{- range .DocComments }}
    //{{ . }}
      {{- end }}
    //{{ template "ClientAllocationComment" . }}
    static ResultOf::{{ .Name }} {{ .Name }}({{ template "StaticCallSyncRequestManagedMethodArguments" . }});
{{ "" }}
      {{- if or .Request .Response }}
        {{- range .DocComments }}
    //{{ . }}
        {{- end }}
    // Caller provides the backing storage for FIDL message via request and response buffers.
    static UnownedResultOf::{{ .Name }} {{ .Name }}({{ template "StaticCallSyncRequestCallerAllocateMethodArguments" . }});
      {{- end }}
{{ "" }}
    {{- end }}
    {{- if .HasEvents }}
    // Handle all possible events defined in this protocol.
    // Blocks to consume exactly one message from the channel, then call the corresponding handler
    // defined in |EventHandlers|. The return status of the handler function is folded with any
    // transport-level errors and returned.
    static zx_status_t HandleEvents(::zx::unowned_channel client_end, EventHandlers handlers);
    {{- end }}
  };

  // Messages are encoded and decoded in-place when these methods are used.
  // Additionally, requests must be already laid-out according to the FIDL wire-format.
  class InPlace final {
    InPlace() = delete;
   public:
{{ "" }}
    {{- range FilterMethodsWithoutReqs .Methods -}}
      {{- range .DocComments }}
    //{{ . }}
      {{- end }}
    static {{ if .HasResponse -}}
    ::fidl::DecodeResult<{{ .Name }}Response>
    {{- else -}}
    ::fidl::internal::StatusAndError
    {{- end }} {{ template "StaticCallSyncRequestInPlaceMethodSignature" . }};
{{ "" }}
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
          {{- if .Result }}
      void {{ template "ReplyCFlavorResultSuccessMethodSignature" . }};
      void {{ template "ReplyCFlavorResultErrorMethodSignature" . }};
          {{- end }}
          {{- if .Response }}
      void {{ template "ReplyCallerAllocateMethodSignature" . }};
            {{- if .Result }}
      void {{ template "ReplyCallerAllocateResultSuccessMethodSignature" . }};
            {{- end }}
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
        {{- if .Transitional -}}
          {{ .Name }}Completer::Sync _completer) { _completer.Close(ZX_ERR_NOT_SUPPORTED); }
        {{- else -}}
          {{ .Name }}Completer::Sync _completer) = 0;
        {{- end }}
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

  // Helper functions to fill in the transaction header in a |DecodedMessage<TransactionalMessage>|.
  class SetTransactionHeaderFor final {
    SetTransactionHeaderFor() = delete;
   public:
  {{- range .Methods }}
    {{- if .HasRequest }}
    static void {{ template "SetTransactionHeaderForRequestMethodSignature" . }};
    {{- end }}
    {{- if .HasResponse }}
    static void {{ template "SetTransactionHeaderForResponseMethodSignature" . }};
    {{- end }}
  {{- end }}
  };
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
static_assert(offsetof({{ $interface.Namespace }}::{{ $interface.Name }}::{{ $method.Name }}Request, {{ $param.Name }}) == {{ $param.OffsetOld }});
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
static_assert(offsetof({{ $interface.Namespace }}::{{ $interface.Name }}::{{ $method.Name }}Response, {{ $param.Name }}) == {{ $param.OffsetOld }});
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
  {{- range .Ordinals.Reads }}
[[maybe_unused]]
constexpr uint64_t {{ .Name }} = {{ .Ordinal }}lu;
  {{- end }}
extern "C" const fidl_type_t {{ .RequestTypeName }};
extern "C" const fidl_type_t {{ .ResponseTypeName }};
extern "C" const fidl_type_t {{ .V1ResponseTypeName }};
{{- end }}

}  // namespace

{{- /* Client-calling functions do not apply to events. */}}
{{- range FilterMethodsWithoutReqs .Methods -}}
{{ "" }}
    {{- template "SyncRequestManagedMethodDefinition" . }}
{{ "" }}
  {{- template "StaticCallSyncRequestManagedMethodDefinition" . }}
  {{- if or .Request .Response }}
{{ "" }}
    {{- template "SyncRequestCallerAllocateMethodDefinition" . }}
{{ "" }}
    {{- template "StaticCallSyncRequestCallerAllocateMethodDefinition" . }}
  {{- end }}
{{ "" }}
  {{- template "StaticCallSyncRequestInPlaceMethodDefinition" . }}
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
      {{- if .Result }}
      {{- template "ReplyCFlavorResultSuccessMethodDefinition" . }}
      {{- template "ReplyCFlavorResultErrorMethodDefinition" . }}
      {{- end }}
      {{- if .Response }}
{{ "" }}
        {{- template "ReplyCallerAllocateMethodDefinition" . }}
        {{- if .Result }}
        {{- template "ReplyCallerAllocateResultSuccessMethodDefinition" . }}
        {{- end }}
      {{- end }}
      {{- if .Response }}
{{ "" }}
        {{- template "ReplyInPlaceMethodDefinition" . }}
      {{- end }}
{{ "" }}
    {{- end }}
  {{- end }}
{{ "" }}

  {{- range .Methods }}
{{ "" }}
    {{- if .HasRequest }}
{{ "" }}
      {{- template "SetTransactionHeaderForRequestMethodDefinition" . }}
    {{- end }}
    {{- if .HasResponse }}
{{ "" }}
      {{- template "SetTransactionHeaderForResponseMethodDefinition" . }}
    {{- end }}
  {{- end }}
{{- end }}

{{- end }}
`
