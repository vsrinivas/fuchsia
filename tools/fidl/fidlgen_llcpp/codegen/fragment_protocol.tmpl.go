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

    {{- if .HasResponse }}
  struct {{ .WireResponse.Self }} final {
    FIDL_ALIGNDECL
        {{- /* Add underscore to prevent name collision */}}
    fidl_message_header_t _hdr;
        {{- range $index, $param := .ResponseArgs }}
    {{ $param.Type }} {{ $param.Name }};
        {{- end }}

    {{- if .ResponseArgs }}
    explicit {{ .WireResponse.Self }}({{ .ResponseArgs | MessagePrototype }})
    {{ .ResponseArgs | InitMessage }} {
      _InitHeader();
    }
    {{- end }}
    {{ .WireResponse.Self }}() {
      _InitHeader();
    }

    static constexpr const fidl_type_t* Type =
    {{- if .ResponseArgs }}
      &{{ .Response.WireCodingTable }};
    {{- else }}
      &::fidl::_llcpp_coding_AnyZeroArgMessageTable;
    {{- end }}
    static constexpr uint32_t MaxNumHandles = {{ .Response.MaxHandles }};
    static constexpr uint32_t PrimarySize = {{ .Response.InlineSize }};
    static constexpr uint32_t MaxOutOfLine = {{ .Response.MaxOutOfLine }};
    static constexpr bool HasFlexibleEnvelope = {{ .Response.IsFlexible }};
    static constexpr bool HasPointer = {{ .Response.HasPointer }};
    static constexpr ::fidl::internal::TransactionalMessageKind MessageKind =
        ::fidl::internal::TransactionalMessageKind::kResponse;

    {{- if .Response.IsResource }}
    void _CloseHandles();
    {{- end }}

    class UnownedEncodedMessage final {
     public:
      UnownedEncodedMessage(uint8_t* _bytes, uint32_t _byte_size
        {{- .ResponseArgs | CommaMessagePrototype }})
          : message_(_bytes, _byte_size, sizeof({{ .WireResponse.Self }}),
      {{- if gt .Response.MaxHandles 0 }}
        handles_, std::min(ZX_CHANNEL_MAX_MSG_HANDLES, MaxNumHandles), 0
      {{- else }}
        nullptr, 0, 0
      {{- end }}
        ) {
        FIDL_ALIGNDECL {{ .WireResponse.Self }} _response{
          {{- .ResponseArgs | ParamNames -}}
        };
        message_.Encode<{{ .WireResponse }}>(&_response);
      }
      UnownedEncodedMessage(uint8_t* bytes, uint32_t byte_size, {{ .WireResponse.Self }}* response)
          : message_(bytes, byte_size, sizeof({{ .WireResponse.Self }}),
      {{- if gt .Response.MaxHandles 0 }}
        handles_, std::min(ZX_CHANNEL_MAX_MSG_HANDLES, MaxNumHandles), 0
      {{- else }}
        nullptr, 0, 0
      {{- end }}
        ) {
        message_.Encode<{{ .WireResponse }}>(response);
      }
      UnownedEncodedMessage(const UnownedEncodedMessage&) = delete;
      UnownedEncodedMessage(UnownedEncodedMessage&&) = delete;
      UnownedEncodedMessage* operator=(const UnownedEncodedMessage&) = delete;
      UnownedEncodedMessage* operator=(UnownedEncodedMessage&&) = delete;

      zx_status_t status() const { return message_.status(); }
#ifdef __Fuchsia__
      const char* status_string() const { return message_.status_string(); }
#endif
      bool ok() const { return message_.status() == ZX_OK; }
      const char* error() const { return message_.error(); }

      ::fidl::OutgoingMessage& GetOutgoingMessage() { return message_; }

#ifdef __Fuchsia__
      template <typename ChannelLike>
      void Write(ChannelLike&& client) { message_.Write(std::forward<ChannelLike>(client)); }
#endif

     private:
      {{- if gt .Response.MaxHandles 0 }}
        zx_handle_disposition_t handles_[std::min(ZX_CHANNEL_MAX_MSG_HANDLES, MaxNumHandles)];
      {{- end }}
      ::fidl::OutgoingMessage message_;
    };

    class OwnedEncodedMessage final {
     public:
      explicit OwnedEncodedMessage({{ .ResponseArgs | MessagePrototype }})
        : message_(bytes_.data(), bytes_.size()
          {{- .ResponseArgs | CommaParamNames }}) {}
      explicit OwnedEncodedMessage({{ .WireResponse }}* response)
        : message_(bytes_.data(), bytes_.size(), response) {}
      OwnedEncodedMessage(const OwnedEncodedMessage&) = delete;
      OwnedEncodedMessage(OwnedEncodedMessage&&) = delete;
      OwnedEncodedMessage* operator=(const OwnedEncodedMessage&) = delete;
      OwnedEncodedMessage* operator=(OwnedEncodedMessage&&) = delete;

      zx_status_t status() const { return message_.status(); }
#ifdef __Fuchsia__
      const char* status_string() const { return message_.status_string(); }
#endif
      bool ok() const { return message_.ok(); }
      const char* error() const { return message_.error(); }

      ::fidl::OutgoingMessage& GetOutgoingMessage() { return message_.GetOutgoingMessage(); }

#ifdef __Fuchsia__
      template <typename ChannelLike>
      void Write(ChannelLike&& client) { message_.Write(std::forward<ChannelLike>(client)); }
#endif

     private:
      {{ .Response.ServerAllocation.ByteBufferType }} bytes_;
      UnownedEncodedMessage message_;
    };

  public:
    class DecodedMessage final : public ::fidl::internal::IncomingMessage {
     public:
      DecodedMessage(uint8_t* bytes, uint32_t byte_actual, zx_handle_info_t* handles = nullptr,
                      uint32_t handle_actual = 0)
          : ::fidl::internal::IncomingMessage(bytes, byte_actual, handles, handle_actual) {
        Decode<{{ .WireResponse }}>();
      }
      DecodedMessage(fidl_incoming_msg_t* msg) : ::fidl::internal::IncomingMessage(msg) {
        Decode<{{ .WireResponse }}>();
      }
      DecodedMessage(const DecodedMessage&) = delete;
      DecodedMessage(DecodedMessage&&) = delete;
      DecodedMessage* operator=(const DecodedMessage&) = delete;
      DecodedMessage* operator=(DecodedMessage&&) = delete;
      {{- if .Response.IsResource }}
      ~DecodedMessage() {
        if (ok() && (PrimaryObject() != nullptr)) {
          PrimaryObject()->_CloseHandles();
        }
      }
      {{- end }}

      {{ .WireResponse.Self }}* PrimaryObject() {
        ZX_DEBUG_ASSERT(ok());
        return reinterpret_cast<{{ .WireResponse }}*>(bytes());
      }

      // Release the ownership of the decoded message. That means that the handles won't be closed
      // When the object is destroyed.
      // After calling this method, the DecodedMessage object should not be used anymore.
      void ReleasePrimaryObject() { ResetBytes(); }
    };

   private:
    void _InitHeader();
  };
    {{- end }}

    {{- if .HasRequest }}
  struct {{ .WireRequest.Self }} final {
    FIDL_ALIGNDECL
        {{- /* Add underscore to prevent name collision */}}
    fidl_message_header_t _hdr;
        {{- range $index, $param := .RequestArgs }}
    {{ $param.Type }} {{ $param.Name }};
        {{- end }}

    {{- if .RequestArgs }}
    explicit {{ .WireRequest.Self }}(zx_txid_t _txid {{- .RequestArgs | CommaMessagePrototype }})
    {{ .RequestArgs | InitMessage }} {
      _InitHeader(_txid);
    }
    {{- end }}
    explicit {{ .WireRequest.Self }}(zx_txid_t _txid) {
      _InitHeader(_txid);
    }

    static constexpr const fidl_type_t* Type =
    {{- if .RequestArgs }}
      &{{ .Request.WireCodingTable }};
    {{- else }}
      &::fidl::_llcpp_coding_AnyZeroArgMessageTable;
    {{- end }}
    static constexpr uint32_t MaxNumHandles = {{ .Request.MaxHandles }};
    static constexpr uint32_t PrimarySize = {{ .Request.InlineSize }};
    static constexpr uint32_t MaxOutOfLine = {{ .Request.MaxOutOfLine }};
    static constexpr uint32_t AltPrimarySize = {{ .Request.InlineSize }};
    static constexpr uint32_t AltMaxOutOfLine = {{ .Request.MaxOutOfLine }};
    static constexpr bool HasFlexibleEnvelope = {{ .Request.IsFlexible }};
    static constexpr bool HasPointer = {{ .Request.HasPointer }};
    static constexpr ::fidl::internal::TransactionalMessageKind MessageKind =
        ::fidl::internal::TransactionalMessageKind::kRequest;

        {{- if and .HasResponse .ResponseArgs }}
    using ResponseType = {{ .WireResponse }};
        {{- end }}

    {{- if .Request.IsResource }}
    void _CloseHandles();
    {{- end }}

    class UnownedEncodedMessage final {
     public:
      UnownedEncodedMessage(uint8_t* _bytes, uint32_t _byte_size, zx_txid_t _txid
        {{- .RequestArgs | CommaMessagePrototype }})
          : message_(_bytes, _byte_size, sizeof({{ .WireRequest.Self }}),
      {{- if gt .Request.MaxHandles 0 }}
        handles_, std::min(ZX_CHANNEL_MAX_MSG_HANDLES, MaxNumHandles), 0
      {{- else }}
        nullptr, 0, 0
      {{- end }}
        ) {
        FIDL_ALIGNDECL {{ .WireRequest.Self }} _request(_txid
            {{- .RequestArgs | CommaParamNames -}}
        );
        message_.Encode<{{ .WireRequest.Self }}>(&_request);
      }
      UnownedEncodedMessage(uint8_t* bytes, uint32_t byte_size, {{ .WireRequest.Self }}* request)
          : message_(bytes, byte_size, sizeof({{ .WireRequest.Self }}),
      {{- if gt .Request.MaxHandles 0 }}
        handles_, std::min(ZX_CHANNEL_MAX_MSG_HANDLES, MaxNumHandles), 0
      {{- else }}
        nullptr, 0, 0
      {{- end }}
        ) {
        message_.Encode<{{ .WireRequest.Self }}>(request);
      }
      UnownedEncodedMessage(const UnownedEncodedMessage&) = delete;
      UnownedEncodedMessage(UnownedEncodedMessage&&) = delete;
      UnownedEncodedMessage* operator=(const UnownedEncodedMessage&) = delete;
      UnownedEncodedMessage* operator=(UnownedEncodedMessage&&) = delete;

      zx_status_t status() const { return message_.status(); }
#ifdef __Fuchsia__
      const char* status_string() const { return message_.status_string(); }
#endif
      bool ok() const { return message_.status() == ZX_OK; }
      const char* error() const { return message_.error(); }

      ::fidl::OutgoingMessage& GetOutgoingMessage() { return message_; }

#ifdef __Fuchsia__
      template <typename ChannelLike>
      void Write(ChannelLike&& client) { message_.Write(std::forward<ChannelLike>(client)); }
#endif

     private:
      {{- if gt .Request.MaxHandles 0 }}
        zx_handle_disposition_t handles_[std::min(ZX_CHANNEL_MAX_MSG_HANDLES, MaxNumHandles)];
      {{- end }}
      ::fidl::OutgoingMessage message_;
    };

    class OwnedEncodedMessage final {
     public:
      explicit OwnedEncodedMessage(zx_txid_t _txid
        {{- .RequestArgs | CommaMessagePrototype }})
        : message_(bytes_.data(), bytes_.size(), _txid
          {{- .RequestArgs | CommaParamNames }}) {}
      explicit OwnedEncodedMessage({{ .WireRequest.Self }}* request)
        : message_(bytes_.data(), bytes_.size(), request) {}
      OwnedEncodedMessage(const OwnedEncodedMessage&) = delete;
      OwnedEncodedMessage(OwnedEncodedMessage&&) = delete;
      OwnedEncodedMessage* operator=(const OwnedEncodedMessage&) = delete;
      OwnedEncodedMessage* operator=(OwnedEncodedMessage&&) = delete;

      zx_status_t status() const { return message_.status(); }
#ifdef __Fuchsia__
      const char* status_string() const { return message_.status_string(); }
#endif
      bool ok() const { return message_.ok(); }
      const char* error() const { return message_.error(); }

      ::fidl::OutgoingMessage& GetOutgoingMessage() { return message_.GetOutgoingMessage(); }

#ifdef __Fuchsia__
      template <typename ChannelLike>
      void Write(ChannelLike&& client) { message_.Write(std::forward<ChannelLike>(client)); }
#endif

     private:
      {{ .Request.ClientAllocation.ByteBufferType }} bytes_;
      UnownedEncodedMessage message_;
    };

  public:
    class DecodedMessage final : public ::fidl::internal::IncomingMessage {
     public:
      DecodedMessage(uint8_t* bytes, uint32_t byte_actual, zx_handle_info_t* handles = nullptr,
                      uint32_t handle_actual = 0)
          : ::fidl::internal::IncomingMessage(bytes, byte_actual, handles, handle_actual) {
        Decode<{{ .WireRequest.Self }}>();
      }
      DecodedMessage(fidl_incoming_msg_t* msg) : ::fidl::internal::IncomingMessage(msg) {
        Decode<{{ .WireRequest.Self }}>();
      }
      DecodedMessage(const DecodedMessage&) = delete;
      DecodedMessage(DecodedMessage&&) = delete;
      DecodedMessage* operator=(const DecodedMessage&) = delete;
      DecodedMessage* operator=(DecodedMessage&&) = delete;
      {{- if .Request.IsResource }}
      ~DecodedMessage() {
        if (ok() && (PrimaryObject() != nullptr)) {
          PrimaryObject()->_CloseHandles();
        }
      }
      {{- end }}

      {{ .WireRequest.Self }}* PrimaryObject() {
        ZX_DEBUG_ASSERT(ok());
        return reinterpret_cast<{{ .WireRequest.Self }}*>(bytes());
      }

      // Release the ownership of the decoded message. That means that the handles won't be closed
      // When the object is destroyed.
      // After calling this method, the DecodedMessage object should not be used anymore.
      void ReleasePrimaryObject() { ResetBytes(); }
    };

   private:
    void _InitHeader(zx_txid_t _txid);
  };
{{ "" }}
    {{- end }}

  {{- end }}

  class EventHandlerInterface {
   public:
    EventHandlerInterface() = default;
    virtual ~EventHandlerInterface() = default;
    {{- range .Events -}}

      {{- range .DocComments }}
    //{{ . }}
      {{- end }}
    virtual void {{ .Name }}({{ .WireResponse }}* event) {}
    {{- end }}
  };
  {{- if .Events }}
{{ "" }}
  class SyncEventHandler : public EventHandlerInterface {
   public:
    SyncEventHandler() = default;

    // Method called when an unknown event is found. This methods gives the status which, in this
    // case, is returned by HandleOneEvent.
    virtual zx_status_t Unknown() = 0;

    // Handle all possible events defined in this protocol.
    // Blocks to consume exactly one message from the channel, then call the corresponding virtual
    // method.
    ::fidl::Result HandleOneEvent(
        ::fidl::UnownedClientEnd<{{ . }}> client_end);
  };
  {{- end }}

#ifdef __Fuchsia__
  // Collection of return types of FIDL calls in this protocol.
  class ResultOf final {
    ResultOf() = delete;
   public:
    {{- range .ClientMethods -}}
    {{- if .HasResponse -}}
{{ "" }}
    {{- end }}
    class {{ .Name }} final : public ::fidl::Result {
     public:
      explicit {{ .Name }}(
          ::fidl::UnownedClientEnd<{{ $protocol }}> _client
          {{- .RequestArgs | CommaMessagePrototype }});
    {{- if .HasResponse }}
      {{ .Name }}(
          ::fidl::UnownedClientEnd<{{ $protocol }}> _client
          {{- .RequestArgs | CommaMessagePrototype }},
          zx_time_t _deadline);
    {{- end }}
      explicit {{ .Name }}(const ::fidl::Result& result) : ::fidl::Result(result) {}
      {{ .Name }}({{ .Name }}&&) = delete;
      {{ .Name }}(const {{ .Name }}&) = delete;
      {{ .Name }}* operator=({{ .Name }}&&) = delete;
      {{ .Name }}* operator=(const {{ .Name }}&) = delete;
      {{- if and .HasResponse .Response.IsResource }}
      ~{{ .Name }}() {
        if (ok()) {
          Unwrap()->_CloseHandles();
        }
      }
      {{- else }}
      ~{{ .Name }}() = default;
      {{- end }}
      {{- if .HasResponse }}

      {{ .WireResponse }}* Unwrap() {
        ZX_DEBUG_ASSERT(ok());
        return reinterpret_cast<{{ .WireResponse }}*>(bytes_.data());
      }
      const {{ .WireResponse }}* Unwrap() const {
        ZX_DEBUG_ASSERT(ok());
        return reinterpret_cast<const {{ .WireResponse }}*>(bytes_.data());
      }

      {{ .WireResponse }}& value() { return *Unwrap(); }
      const {{ .WireResponse }}& value() const { return *Unwrap(); }

      {{ .WireResponse }}* operator->() { return &value(); }
      const {{ .WireResponse.Self }}* operator->() const { return &value(); }

      {{ .WireResponse.Self }}& operator*() { return value(); }
      const {{ .WireResponse.Self }}& operator*() const { return value(); }
      {{- end }}

     private:
      {{- if .HasResponse }}
        {{ .Response.ClientAllocation.ByteBufferType }} bytes_;
      {{- end }}
    };
    {{- end }}
  };

  // Collection of return types of FIDL calls in this protocol,
  // when the caller-allocate flavor or in-place call is used.
  class UnownedResultOf final {
    UnownedResultOf() = delete;

   public:
    {{- range .ClientMethods -}}
    class {{ .Name }} final : public ::fidl::Result {
     public:
      explicit {{ .Name }}(
          ::fidl::UnownedClientEnd<{{ $protocol }}> _client
        {{- if .RequestArgs -}}
          , uint8_t* _request_bytes, uint32_t _request_byte_capacity
        {{- end -}}
        {{- .RequestArgs | CommaMessagePrototype }}
        {{- if .HasResponse -}}
          , uint8_t* _response_bytes, uint32_t _response_byte_capacity
        {{- end -}});
      explicit {{ .Name }}(const ::fidl::Result& result) : ::fidl::Result(result) {}
      {{ .Name }}({{ .Name }}&&) = delete;
      {{ .Name }}(const {{ .Name }}&) = delete;
      {{ .Name }}* operator=({{ .Name }}&&) = delete;
      {{ .Name }}* operator=(const {{ .Name }}&) = delete;
      {{- if and .HasResponse .Response.IsResource }}
      ~{{ .Name }}() {
        if (ok()) {
          Unwrap()->_CloseHandles();
        }
      }
      {{- else }}
      ~{{ .Name }}() = default;
      {{- end }}
      {{- if .HasResponse }}

      {{ .WireResponse.Self }}* Unwrap() {
        ZX_DEBUG_ASSERT(ok());
        return reinterpret_cast<{{ .WireResponse }}*>(bytes_);
      }
      const {{ .WireResponse.Self }}* Unwrap() const {
        ZX_DEBUG_ASSERT(ok());
        return reinterpret_cast<const {{ .WireResponse }}*>(bytes_);
      }

      {{ .WireResponse.Self }}& value() { return *Unwrap(); }
      const {{ .WireResponse.Self }}& value() const { return *Unwrap(); }

      {{ .WireResponse.Self }}* operator->() { return &value(); }
      const {{ .WireResponse.Self }}* operator->() const { return &value(); }

      {{ .WireResponse.Self }}& operator*() { return value(); }
      const {{ .WireResponse.Self }}& operator*() const { return value(); }

     private:
      uint8_t* bytes_;
      {{- end }}
    };
    {{- end }}
  };

  // Methods to make a sync FIDL call directly on an unowned channel or a
  // const reference to a |fidl::ClientEnd<{{ .WireType }}>|,
  // avoiding setting up a client.
  class Call final {
    Call() = delete;
   public:
{{ "" }}
    {{- /* Client-calling functions do not apply to events. */}}
    {{- range .ClientMethods -}}
      {{- range .DocComments }}
    //{{ . }}
      {{- end }}
    //{{ template "ClientAllocationComment" . }}
    static ResultOf::{{ .Name }} {{ .Name }}(
          ::fidl::UnownedClientEnd<{{ .Protocol }}> _client_end
          {{- .RequestArgs | CommaParams }}) {
      return ResultOf::{{ .Name }}(_client_end
        {{- .RequestArgs | CommaParamNames -}}
        );
    }
{{ "" }}
      {{- if or .RequestArgs .ResponseArgs }}
        {{- range .DocComments }}
    //{{ . }}
        {{- end }}
    // Caller provides the backing storage for FIDL message via request and response buffers.
    static UnownedResultOf::{{ .Name }} {{ .Name }}({{ template "StaticCallSyncRequestCallerAllocateMethodArguments" . }}) {
      return UnownedResultOf::{{ .Name }}(_client_end
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

#endif

  using EventSender = {{ .WireEventSender }};
  using WeakEventSender = {{ .WireWeakEventSender }};
};

{{- EnsureNamespace "::" }}
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
