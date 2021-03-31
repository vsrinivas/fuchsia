// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentProtocolTmpl = `
{{- define "ProtocolForwardDeclaration" }}
{{ EnsureNamespace . }}
class {{ .Name }};
{{- end }}

{{- define "ForwardMessageParamsUnwrapTypedChannels" -}}
  {{- range $index, $param := . -}}
    {{- if $index }}, {{ end -}} std::move(
      {{- if or (eq .Type.Kind TypeKinds.Protocol) (eq .Type.Kind TypeKinds.Request) -}}
        {{ $param.Name }}.channel()
      {{- else -}}
        {{ $param.Name }}
      {{- end -}}
    )
  {{- end -}}
{{- end }}


{{- define "ClientAllocationComment" -}}
{{- if SyncCallTotalStackSize . }} Allocates {{ SyncCallTotalStackSize . }} bytes of {{ "" }}
{{- if not .Request.ClientAllocation.IsStack -}} response {{- else -}}
  {{- if not .Response.ClientAllocation.IsStack -}} request {{- else -}} message {{- end -}}
{{- end }} buffer on the stack. {{- end }}
{{- if and .Request.ClientAllocation.IsStack .Response.ClientAllocation.IsStack -}}
{{ "" }} No heap allocation necessary.
{{- else }}
  {{- if not .Request.ClientAllocation.IsStack }} Request is heap-allocated. {{- end }}
  {{- if not .Response.ClientAllocation.IsStack }} Response is heap-allocated. {{- end }}
{{- end }}
{{- end }}

{{- define "ProtocolDeclaration" }}
{{- $protocol := . }}
{{ "" }}
  {{- range .Methods }}
{{ EnsureNamespace .Request.WireCodingTable }}
extern "C" const fidl_type_t {{ .Request.WireCodingTable.Name }};
{{ EnsureNamespace .Response.WireCodingTable }}
extern "C" const fidl_type_t {{ .Response.WireCodingTable.Name }};
  {{- end }}
{{ "" }}
{{ EnsureNamespace . }}

{{- range .DocComments }}
//{{ . }}
{{- end }}
class {{ .Name }} final {
  {{ .Name }}() = delete;
 public:
  {{- if .DiscoverableName }}
    static constexpr char Name[] = {{ .DiscoverableName }};
  {{- end }}

  {{ "" }}
  {{- range .Methods }}
    {{- range .DocComments }}
      //{{ . }}
    {{- end }}
    class {{ .Marker.Self }} final {
      {{ .Marker.Self }}() = delete;
    };
  {{- end }}

  {{ "" }}
  {{- range .Methods }}
    {{- if .HasResponse }}
      using {{ .WireResponseAlias.Self }} = {{ .WireResponse }};
    {{- end }}

    {{- if .HasRequest }}
      using {{ .WireRequestAlias.Self }} = {{ .WireRequest }};
    {{- end }}
  {{- end }}

  {{- if .Events }}
    using EventHandlerInterface = {{ .WireEventHandlerInterface }};
    using SyncEventHandler = {{ .WireSyncEventHandler }};
  {{- end }}

{{- IfdefFuchsia -}}
  // Collection of return types of FIDL calls in this protocol.
  class ResultOf final {
    ResultOf() = delete;
   public:
    {{- range .ClientMethods -}}
      using {{ .Name }}  = {{ .WireResult }};
    {{- end }}
  };

  // Collection of return types of FIDL calls in this protocol,
  // when the caller-allocate flavor or in-place call is used.
  class UnownedResultOf final {
    UnownedResultOf() = delete;

   public:
    {{- range .ClientMethods -}}
    using {{ .Name }} = {{ .WireUnownedResult }};
    {{- end }}
  };

  // Methods to make a sync FIDL call directly on an unowned channel or a
  // const reference to a |fidl::ClientEnd<{{ .WireType }}>|,
  // avoiding setting up a client.
  using Call = {{ .WireCall }};

  using SyncClient = fidl::WireSyncClient<{{ . }}>;

{{ template "ClientForwardDeclaration" . }}

  using Interface = {{ .WireInterface }};
  {{- if .ShouldEmitTypedChannelCascadingInheritance }}
  using RawChannelInterface = {{ .WireRawChannelInterface }};
  {{- end }}


  // Attempts to dispatch the incoming message to a handler function in the server implementation.
  // If there is no matching handler, it returns false, leaving the message and transaction intact.
  // In all other cases, it consumes the message and returns true.
  // It is possible to chain multiple TryDispatch functions in this manner.
  static ::fidl::DispatchResult TryDispatch{{ template "SyncServerDispatchMethodSignature" }};

  // Dispatches the incoming message to one of the handlers functions in the protocol.
  // If there is no matching handler, it closes all the handles in |msg| and closes the channel with
  // a |ZX_ERR_NOT_SUPPORTED| epitaph, before returning false. The message should then be discarded.
  static ::fidl::DispatchResult Dispatch{{ template "SyncServerDispatchMethodSignature" }};

  // Same as |Dispatch|, but takes a |void*| instead of |Interface*|.
  // Only used with |fidl::BindServer| to reduce template expansion.
  // Do not call this method manually. Use |Dispatch| instead.
  static ::fidl::DispatchResult TypeErasedDispatch(
      void* impl, fidl_incoming_msg_t* msg, ::fidl::Transaction* txn) {
    return Dispatch(static_cast<Interface*>(impl), msg, txn);
  }

{{- EndifFuchsia -}}

  using EventSender = {{ .WireEventSender }};
  using WeakEventSender = {{ .WireWeakEventSender }};
};

{{- range .Methods }}
  {{- if .HasRequest }}
    {{- template "MethodRequest" . }}
  {{- end }}
  {{- if .HasResponse }}
    {{- template "MethodResponse" . }}
  {{- end }}
{{- end }}

{{- range .ClientMethods -}}
  {{- template "MethodResultOf" . }}
  {{- template "MethodUnownedResultOf" . }}
{{- end }}

{{- EnsureNamespace "::" }}

// Methods to make a sync FIDL call directly on an unowned channel or a
// const reference to a |fidl::ClientEnd<{{ .WireType }}>|,
// avoiding setting up a client.
template<>
class {{ .WireCall }} final {
  {{ .WireCall.Self }}() = delete;
 public:
{{ "" }}
  {{- /* Client-calling functions do not apply to events. */}}
  {{- range .ClientMethods -}}
    {{- range .DocComments }}
  //{{ . }}
    {{- end }}
  //{{ template "ClientAllocationComment" . }}
  static {{ .WireResult }} {{ .Name }}(
        ::fidl::UnownedClientEnd<{{ .Protocol }}> _client_end
        {{- .RequestArgs | CommaParams }}) {
    return {{ .WireResult }}(_client_end
      {{- .RequestArgs | CommaParamNames -}}
      );
  }
{{ "" }}
    {{- if or .RequestArgs .ResponseArgs }}
      {{- range .DocComments }}
  //{{ . }}
      {{- end }}
  // Caller provides the backing storage for FIDL message via request and response buffers.
  static {{ .WireUnownedResult }} {{ .Name }}({{ template "StaticCallSyncRequestCallerAllocateMethodArguments" . }}) {
    return {{ .WireUnownedResult }}(_client_end
      {{- if .RequestArgs -}}
        , _request_buffer.data, _request_buffer.capacity
      {{- end -}}
        {{- .RequestArgs | CommaParamNames -}}
      {{- if .HasResponse -}}
        , _response_buffer.data, _response_buffer.capacity
      {{- end -}});
  }
    {{- end }}
{{ "" }}
  {{- end }}
};

template<>
class {{ .WireEventHandlerInterface }} {
public:
  {{ .WireEventHandlerInterface.Self }}() = default;
  virtual ~{{ .WireEventHandlerInterface.Self }}() = default;
  {{- range .Events -}}

    {{- range .DocComments }}
  //{{ . }}
    {{- end }}
  virtual void {{ .Name }}({{ .WireResponse }}* event) {}
  {{- end }}
};

template<>
class {{ .WireSyncEventHandler }} : public {{ .WireEventHandlerInterface }} {
public:
  {{ .WireSyncEventHandler.Self }}() = default;

  // Method called when an unknown event is found. This methods gives the status which, in this
  // case, is returned by HandleOneEvent.
  virtual zx_status_t Unknown() = 0;

  // Handle all possible events defined in this protocol.
  // Blocks to consume exactly one message from the channel, then call the corresponding virtual
  // method.
  ::fidl::Result HandleOneEvent(
      ::fidl::UnownedClientEnd<{{ . }}> client_end);
};

template<>
class {{ .WireSyncClient }} final {
  public:
   WireSyncClient() = default;

   explicit WireSyncClient(::fidl::ClientEnd<{{ . }}> client_end)
       : client_end_(std::move(client_end)) {}

   ~WireSyncClient() = default;
   WireSyncClient(WireSyncClient&&) = default;
   WireSyncClient& operator=(WireSyncClient&&) = default;

   const ::fidl::ClientEnd<{{ . }}>& client_end() const { return client_end_; }
   ::fidl::ClientEnd<{{ . }}>& client_end() { return client_end_; }

   const ::zx::channel& channel() const { return client_end_.channel(); }
   ::zx::channel* mutable_channel() { return &client_end_.channel(); }
{{ "" }}
   {{- /* Client-calling functions do not apply to events. */}}
   {{- range .ClientMethods -}}
     {{- range .DocComments }}
   //{{ . }}
     {{- end }}
   //{{ template "ClientAllocationComment" . }}
   {{ .WireResult }} {{ .Name }}({{ .RequestArgs | Params }}) {
     return {{ .WireResult }}(this->client_end()
       {{- .RequestArgs | CommaParamNames -}});
   }
{{ "" }}
     {{- if or .RequestArgs .ResponseArgs }}
       {{- range .DocComments }}
   //{{ . }}
       {{- end }}
   // Caller provides the backing storage for FIDL message via request and response buffers.
   {{ .WireUnownedResult }} {{ .Name }}({{ template "SyncRequestCallerAllocateMethodArguments" . }}) {
     return {{ .WireUnownedResult }}(this->client_end()
       {{- if .RequestArgs -}}
         , _request_buffer.data, _request_buffer.capacity
       {{- end -}}
         {{- .RequestArgs | CommaParamNames -}}
       {{- if .HasResponse -}}
         , _response_buffer.data, _response_buffer.capacity
       {{- end -}});
   }
     {{- end }}
{{ "" }}
   {{- end }}
   {{- if .Events }}
   // Handle all possible events defined in this protocol.
   // Blocks to consume exactly one message from the channel, then call the corresponding virtual
   // method defined in |SyncEventHandler|. The return status of the handler function is folded with
   // any transport-level errors and returned.
   ::fidl::Result HandleOneEvent({{ .WireSyncEventHandler  }}& event_handler) {
     return event_handler.HandleOneEvent(client_end_);
   }
   {{- end }}
  private:
    ::fidl::ClientEnd<{{ . }}> client_end_;
};


{{ "" }}
// Pure-virtual interface to be implemented by a server.
// This interface uses typed channels (i.e. |fidl::ClientEnd<SomeProtocol>|
// and |fidl::ServerEnd<SomeProtocol>|).
template<>
class {{ .WireInterface }}  : public ::fidl::internal::IncomingMessageDispatcher {
  public:
  WireInterface() = default;
  virtual ~WireInterface() = default;

  // The marker protocol type within which this |WireInterface| class is defined.
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
      {{- range .DocComments }}
  //{{ . }}
      {{- end }}
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

    // The marker protocol type within which this |RawChannelInterface| class is defined.
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

{{- define "ProtocolTraits" -}}
{{ $protocol := . -}}
{{ range .Methods -}}
{{ $method := . -}}
{{- if .HasRequest }}

template <>
struct IsFidlType<{{ .WireRequest }}> : public std::true_type {};
template <>
struct IsFidlMessage<{{ .WireRequest }}> : public std::true_type {};
static_assert(sizeof({{ .WireRequest }})
    == {{ .WireRequest }}::PrimarySize);
{{- range $index, $param := .RequestArgs }}
static_assert(offsetof({{ $method.WireRequest }}, {{ $param.Name }}) == {{ $param.Offset }});
{{- end }}
{{- end }}
{{- if .HasResponse }}

template <>
struct IsFidlType<{{ .WireResponse }}> : public std::true_type {};
template <>
struct IsFidlMessage<{{ .WireResponse }}> : public std::true_type {};
static_assert(sizeof({{ .WireResponse }})
    == {{ .WireResponse }}::PrimarySize);
{{- range $index, $param := .ResponseArgs }}
static_assert(offsetof({{ $method.WireResponse }}, {{ $param.Name }}) == {{ $param.Offset }});
{{- end }}
{{- end }}
{{- end }}
{{- end }}

{{- define "ProtocolDefinition" }}
{{ $protocol := . -}}

{{- range .Methods }}
{{ EnsureNamespace .OrdinalName }}
[[maybe_unused]]
constexpr uint64_t {{ .OrdinalName.Name }} = {{ .Ordinal }}lu;
{{ EnsureNamespace .Request.WireCodingTable }}
extern "C" const fidl_type_t {{ .Request.WireCodingTable.Name }};
{{ EnsureNamespace .Response.WireCodingTable }}
extern "C" const fidl_type_t {{ .Response.WireCodingTable.Name }};
{{- end }}

{{- /* Client-calling functions do not apply to events. */}}
{{- range .ClientMethods -}}
{{ "" }}
    {{- template "SyncRequestManagedMethodDefinition" . }}
  {{- if or .RequestArgs .ResponseArgs }}
{{ "" }}
    {{- template "SyncRequestCallerAllocateMethodDefinition" . }}
  {{- end }}
{{ "" }}
{{- end }}

{{- range .ClientMethods }}
{{ "" }}
  {{- template "ClientSyncRequestManagedMethodDefinition" . }}
  {{- if or .RequestArgs .ResponseArgs }}
{{ "" }}
    {{- template "ClientSyncRequestCallerAllocateMethodDefinition" . }}
  {{- end }}
  {{- if .HasResponse }}
{{ "" }}
    {{- template "ClientAsyncRequestManagedMethodDefinition" . }}
  {{- end }}
{{- end }}
{{ template "ClientDispatchDefinition" . }}
{{ "" }}

{{- if .Events }}
  {{- template "EventHandlerHandleOneEventMethodDefinition" . }}
{{- end }}

{{- /* Server implementation */}}
{{ template "SyncServerTryDispatchMethodDefinition" . }}
{{ template "SyncServerDispatchMethodDefinition" . }}

{{- if .Methods }}
{{ "" }}
  {{- range .TwoWayMethods -}}
{{ "" }}
    {{- template "ReplyManagedMethodDefinition" . }}
    {{- if .Result }}
      {{- template "ReplyManagedResultSuccessMethodDefinition" . }}
      {{- template "ReplyManagedResultErrorMethodDefinition" . }}
    {{- end }}
    {{- if .ResponseArgs }}
{{ "" }}
      {{- template "ReplyCallerAllocateMethodDefinition" . }}
      {{- if .Result }}
        {{- template "ReplyCallerAllocateResultSuccessMethodDefinition" . }}
      {{- end }}
    {{- end }}
{{ "" }}
  {{- end }}
{{ "" }}
{{- EnsureNamespace "" }}
  {{- range .Methods }}
{{ "" }}
    {{- if .HasRequest }}
{{ "" }}
    void {{ .WireRequest }}::_InitHeader(zx_txid_t _txid) {
      fidl_init_txn_header(&_hdr, _txid, {{ .OrdinalName }});
    }
      {{- if .Request.IsResource }}

    void {{ .WireRequest }}::_CloseHandles() {
      {{- range .RequestArgs }}
        {{- CloseHandles . false false }}
      {{- end }}
    }
      {{- end }}
    {{- end }}
    {{- if .HasResponse }}
{{ "" }}
    void {{ .WireResponse }}::_InitHeader() {
      fidl_init_txn_header(&_hdr, 0, {{ .OrdinalName }});
    }
      {{- if .Response.IsResource }}

    void {{ .WireResponse }}::_CloseHandles() {
      {{- range .ResponseArgs }}
          {{- CloseHandles . false false }}
      {{- end }}
    }
      {{- end }}
    {{- end }}
  {{- end }}
{{- end }}

{{- end }}
`
