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
  using Call = {{ .WireCaller }};

  using SyncClient = fidl::WireSyncClient<{{ . }}>;

  {{- IfdefFuchsia -}}
    using AsyncEventHandler = {{ .WireAsyncEventHandler }};
    {{- range .TwoWayMethods }}
      class {{ .WireResponseContext.Self }};
    {{- end }}
    using ClientImpl = {{ .WireClientImpl }};
  {{- EndifFuchsia -}}

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

{{- EndifFuchsia -}}

  using EventSender = {{ .WireEventSender }};
  using WeakEventSender = {{ .WireWeakEventSender }};
};

{{- range .Methods }}
  {{- if .HasRequest }}
    {{- template "MethodRequestDeclaration" . }}
  {{- end }}
  {{- if .HasResponse }}
    {{- template "MethodResponseDeclaration" . }}
  {{- end }}
{{- end }}

{{- range .ClientMethods -}}
  {{- template "MethodResultDeclaration" . }}
  {{- template "MethodUnownedResultDeclaration" . }}
{{- end }}

{{- EnsureNamespace "::" }}

// Methods to make a sync FIDL call directly on an unowned channel or a
// const reference to a |fidl::ClientEnd<{{ .WireType }}>|,
// avoiding setting up a client.
template<>
class {{ .WireCaller }} final {
 public:
  explicit {{ .WireCaller.Self }}(::fidl::UnownedClientEnd<{{ . }}> client_end) :
    client_end_(client_end) {}
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

    {{- range .DocComments }}
      //{{ . }}
    {{- end }}
    //{{ template "ClientAllocationComment" . }}
    {{ .WireResult }} {{ .Name }}({{- .RequestArgs | Params }}) && {
      return {{ .WireResult }}(client_end_
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

      {{- range .DocComments }}
        //{{ . }}
      {{- end }}
      // Caller provides the backing storage for FIDL message via request and response buffers.
      {{ .WireUnownedResult }} {{ .Name }}({{ template "SyncRequestCallerAllocateMethodArguments" . }}) && {
        return {{ .WireUnownedResult }}(client_end_
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
 private:
  ::fidl::UnownedClientEnd<{{ . }}> client_end_;
};

{{- template "ProtocolEventHandlerDeclaration" . }}

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


  {{- template "ProtocolInterfaceDeclaration" . }}
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
    {{- template "MethodResultDefinition" . }}
  {{- if or .RequestArgs .ResponseArgs }}
{{ "" }}
    {{- template "MethodUnownedResultDefinition" . }}
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

  {{- range .Methods }}

    {{- if .HasRequest }}{{ template "MethodRequestDefinition" . }}{{ end }}
    {{ "" }}

    {{- if .HasResponse }}{{ template "MethodResponseDefinition" . }}{{ end }}
    {{ "" }}

  {{- end }}
{{- end }}

{{- end }}
`
