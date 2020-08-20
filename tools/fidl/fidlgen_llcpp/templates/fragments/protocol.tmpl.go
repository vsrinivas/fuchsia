// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fragments

const Protocol = `
{{- define "ArgumentDeclaration" -}}
  {{- if eq .Type.LLFamily TrivialCopy }}
  {{ .Type.LLDecl }} {{ .Name }}
  {{- else if eq .Type.LLFamily Reference }}
  {{ .Type.LLDecl }}& {{ .Name }}
  {{- else if eq .Type.LLFamily String }}
  const {{ .Type.LLDecl }}& {{ .Name }}
  {{- else if eq .Type.LLFamily Vector }}
  {{ .Type.LLDecl }}& {{ .Name }}
  {{- end }}
{{- end }}

{{- /* Defines the arguments for a response method/constructor. */}}
{{- define "MessagePrototype" -}}
  {{- range $index, $param := . -}}
    {{- if $index }}, {{ end}}{{ template "ArgumentDeclaration" $param }}
  {{- end -}}
{{- end }}

{{- /* Defines the arguments for a request method/constructor. */}}
{{- define "CommaMessagePrototype" -}}
  {{- range $param := . -}}
    , {{ template "ArgumentDeclaration" $param }}
  {{- end -}}
{{- end }}

{{- /* Defines the initialization of all the fields for a message constructor. */}}
{{- define "InitMessage" -}}
  {{- range $index, $param := . }}
  {{- if $index }}, {{- else }}: {{- end}}
    {{- if eq $param.Type.LLFamily TrivialCopy }}
      {{ $param.Name }}({{ $param.Name }})
    {{- else if eq $param.Type.LLFamily Reference }}
      {{ $param.Name }}(std::move({{ $param.Name }}))
    {{- else if eq $param.Type.LLFamily String }}
      {{ $param.Name }}(::fidl::unowned_ptr_t<const char>({{ $param.Name }}.data()), {{ $param.Name }}.size())
    {{- else if eq $param.Type.LLFamily Vector }}
      {{ $param.Name }}(::fidl::unowned_ptr_t<{{ $param.Type.ElementType.LLDecl }}>({{ $param.Name }}.mutable_data()), {{ $param.Name }}.count())
    {{- end }}
  {{- end }}
{{- end }}

{{- define "ProtocolForwardDeclaration" }}
class {{ .Name }};
{{- end }}

{{- define "RequestCodingTable" -}}
&{{ .RequestTypeName }}
{{- end }}

{{- define "ResponseCodingTable" -}}
&{{ .ResponseTypeName }}
{{- end }}

{{- /* All the parameters for a method which value content. */}}
{{- define "ForwardParams" -}}
  {{- range $index, $param := . -}}
    {{- if $index }}, {{ end -}} std::move({{ $param.Name }})
  {{- end -}}
{{- end }}

{{- /* All the parameters for a response method/constructor which uses values with references */}}
{{- /* or trivial copy. */}}
{{- define "PassthroughMessageParams" -}}
  {{- range $index, $param := . }}
    {{- if $index }}, {{- end }} {{ $param.Name }}
  {{- end }}
{{- end }}

{{- /* All the parameters for a request method/constructor which uses values with references */}}
{{- /* or trivial copy. */}}
{{- define "CommaPassthroughMessageParams" -}}
  {{- range $index, $param := . }}
    , {{ $param.Name }}
  {{- end }}
{{- end }}

{{- define "ClientAllocationComment" -}}
{{- $context := .LLProps.ClientContext }}
{{- if StackUse $context }} Allocates {{ StackUse $context }} bytes of {{ "" }}
{{- if not $context.StackAllocRequest -}} response {{- else -}}
  {{- if not $context.StackAllocResponse -}} request {{- else -}} message {{- end -}}
{{- end }} buffer on the stack. {{- end }}
{{- if and $context.StackAllocRequest $context.StackAllocResponse }} No heap allocation necessary.
{{- else }}
  {{- if not $context.StackAllocRequest }} Request is heap-allocated. {{- end }}
  {{- if not $context.StackAllocResponse }} Response is heap-allocated. {{- end }}
{{- end }}
{{- end }}

{{- define "RequestSentSize"}}
  {{- if gt .RequestSentMaxSize 65536 -}}
  ZX_CHANNEL_MAX_MSG_BYTES
  {{- else -}}
  {{ .Name }}Request::PrimarySize + {{ .Name }}Request::MaxOutOfLine
  {{- end -}}
{{- end }}

{{- define "ResponseSentSize"}}
  {{- if gt .ResponseSentMaxSize 65536 -}}
  ZX_CHANNEL_MAX_MSG_BYTES
  {{- else -}}
  {{ .Name }}Response::PrimarySize + {{ .Name }}Response::MaxOutOfLine
  {{- end -}}
{{- end }}

{{- define "ResponseReceivedSize"}}
  {{- if gt .ResponseReceivedMaxSize 65536 -}}
  ZX_CHANNEL_MAX_MSG_BYTES
  {{- else -}}
  {{ .Name }}Response::PrimarySize + {{ .Name }}Response::MaxOutOfLine
  {{- end -}}
{{- end }}

{{- define "ResponseReceivedByteAccess" }}
  {{- if gt .ResponseReceivedMaxSize 512 -}}
  bytes_->data()
  {{- else -}}
  bytes_
  {{- end -}}
{{- end }}

{{- define "ProtocolDeclaration" }}
{{- $protocol := . }}
{{ "" }}
  {{- range .Methods }}
extern "C" const fidl_type_t {{ .RequestTypeName }};
extern "C" const fidl_type_t {{ .ResponseTypeName }};
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
  struct {{ .Name }}Response final {
    FIDL_ALIGNDECL
        {{- /* Add underscore to prevent name collision */}}
    fidl_message_header_t _hdr;
        {{- range $index, $param := .Response }}
    {{ $param.Type.LLDecl }} {{ $param.Name }};
        {{- end }}

    {{- if .Response }}
    explicit {{ .Name }}Response({{ template "MessagePrototype" .Response }})
    {{ template "InitMessage" .Response }} {
      _InitHeader();
    }
    {{- end }}
    {{ .Name }}Response() {
      _InitHeader();
    }

    static constexpr const fidl_type_t* Type =
    {{- if .Response }}
      {{ template "ResponseCodingTable" . }};
    {{- else }}
      &::fidl::_llcpp_coding_AnyZeroArgMessageTable;
    {{- end }}
    static constexpr uint32_t MaxNumHandles = {{ .ResponseMaxHandles }};
    static constexpr uint32_t PrimarySize = {{ .ResponseSize }};
    static constexpr uint32_t MaxOutOfLine = {{ .ResponseMaxOutOfLine }};
    static constexpr bool HasFlexibleEnvelope = {{ .ResponseFlexible }};
    static constexpr bool HasPointer = {{ .ResponseHasPointer }};
    static constexpr bool IsResource = {{ .ResponseIsResource }};
    static constexpr ::fidl::internal::TransactionalMessageKind MessageKind =
        ::fidl::internal::TransactionalMessageKind::kResponse;

   private:
    void _InitHeader();
  };
    {{- end }}

    {{- if .HasRequest }}
  struct {{ .Name }}Request final {
    FIDL_ALIGNDECL
        {{- /* Add underscore to prevent name collision */}}
    fidl_message_header_t _hdr;
        {{- range $index, $param := .Request }}
    {{ $param.Type.LLDecl }} {{ $param.Name }};
        {{- end }}

    {{- if .Request }}
    explicit {{ .Name }}Request(zx_txid_t _txid {{- template "CommaMessagePrototype" .Request }})
    {{ template "InitMessage" .Request }} {
      _InitHeader(_txid);
    }
    {{- end }}
    explicit {{ .Name }}Request(zx_txid_t _txid) {
      _InitHeader(_txid);
    }

    static constexpr const fidl_type_t* Type =
    {{- if .Request }}
      {{ template "RequestCodingTable" . }};
    {{- else }}
      &::fidl::_llcpp_coding_AnyZeroArgMessageTable;
    {{- end }}
    static constexpr uint32_t MaxNumHandles = {{ .RequestMaxHandles }};
    static constexpr uint32_t PrimarySize = {{ .RequestSize }};
    static constexpr uint32_t MaxOutOfLine = {{ .RequestMaxOutOfLine }};
    static constexpr uint32_t AltPrimarySize = {{ .RequestSize }};
    static constexpr uint32_t AltMaxOutOfLine = {{ .RequestMaxOutOfLine }};
    static constexpr bool HasFlexibleEnvelope = {{ .RequestFlexible }};
    static constexpr bool HasPointer = {{ .RequestHasPointer }};
    static constexpr bool IsResource = {{ .RequestIsResource }};
    static constexpr ::fidl::internal::TransactionalMessageKind MessageKind =
        ::fidl::internal::TransactionalMessageKind::kRequest;

        {{- if and .HasResponse .Response }}
    using ResponseType = {{ .Name }}Response;
        {{- end }}

   private:
    void _InitHeader(zx_txid_t _txid);
  };
{{ "" }}
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

  // Collection of return types of FIDL calls in this protocol.
  class ResultOf final {
    ResultOf() = delete;
   public:
    {{- range FilterMethodsWithoutReqs .Methods -}}
    {{- if .HasResponse -}}
{{ "" }}
    {{- end }}
    class {{ .Name }} final : public ::fidl::Result {
     public:
      explicit {{ .Name }}(zx_handle_t _client {{- template "CommaMessagePrototype" .Request }});
      explicit {{ .Name }}(const ::fidl::Result& result) : ::fidl::Result(result) {}
      {{ .Name }}({{ .Name }}&&) = delete;
      {{ .Name }}(const {{ .Name }}&) = delete;
      {{ .Name }}* operator=({{ .Name }}&&) = delete;
      {{ .Name }}* operator=(const {{ .Name }}&) = delete;
      {{- if and .HasResponse .ResponseIsResource }}
      ~{{ .Name }}() {
        if (ok()) {
          fidl_close_handles({{ .Name }}Response::Type, {{- template "ResponseReceivedByteAccess" . }}, nullptr);
        }
      }
      {{- else }}
      ~{{ .Name }}() = default;
      {{- end }}
      {{- if .HasResponse }}

      {{ .Name }}Response* Unwrap() {
        ZX_DEBUG_ASSERT(ok());
        return reinterpret_cast<{{ .Name }}Response*>({{- template "ResponseReceivedByteAccess" . }});
      }
      const {{ .Name }}Response* Unwrap() const {
        ZX_DEBUG_ASSERT(ok());
        return reinterpret_cast<const {{ .Name }}Response*>({{- template "ResponseReceivedByteAccess" . }});
      }

      {{ .Name }}Response& value() { return *Unwrap(); }
      const {{ .Name }}Response& value() const { return *Unwrap(); }

      {{ .Name }}Response* operator->() { return &value(); }
      const {{ .Name }}Response* operator->() const { return &value(); }

      {{ .Name }}Response& operator*() { return value(); }
      const {{ .Name }}Response& operator*() const { return value(); }
      {{- end }}

     private:
      {{- if .HasResponse }}
        {{- if gt .ResponseReceivedMaxSize 512 }}
        std::unique_ptr<::fidl::internal::AlignedBuffer<{{ template "ResponseReceivedSize" . }}>> bytes_;
        {{- else }}
        FIDL_ALIGNDECL
        uint8_t bytes_[{{ .Name }}Response::PrimarySize + {{ .Name }}Response::MaxOutOfLine];
        {{- end }}
      {{- end }}
    };
    {{- end }}
  };

  // Collection of return types of FIDL calls in this protocol,
  // when the caller-allocate flavor or in-place call is used.
  class UnownedResultOf final {
    UnownedResultOf() = delete;

   public:
    {{- range FilterMethodsWithoutReqs .Methods -}}
    class {{ .Name }} final : public ::fidl::Result {
     public:
      explicit {{ .Name }}(zx_handle_t _client
        {{- if .Request -}}
        , uint8_t* _request_bytes, uint32_t _request_byte_capacity
        {{- end -}}
        {{- template "CommaMessagePrototype" .Request }}
        {{- if .HasResponse -}}
        , uint8_t* _response_bytes, uint32_t _response_byte_capacity
        {{- end -}});
      explicit {{ .Name }}(const ::fidl::Result& result) : ::fidl::Result(result) {}
      {{ .Name }}({{ .Name }}&&) = delete;
      {{ .Name }}(const {{ .Name }}&) = delete;
      {{ .Name }}* operator=({{ .Name }}&&) = delete;
      {{ .Name }}* operator=(const {{ .Name }}&) = delete;
      {{- if and .HasResponse .ResponseIsResource }}
      ~{{ .Name }}() {
        if (ok()) {
          fidl_close_handles({{ .Name }}Response::Type, bytes_, nullptr);
        }
      }
      {{- else }}
      ~{{ .Name }}() = default;
      {{- end }}
      {{- if .HasResponse }}

      {{ .Name }}Response* Unwrap() {
        ZX_DEBUG_ASSERT(ok());
        return reinterpret_cast<{{ .Name }}Response*>(bytes_);
      }
      const {{ .Name }}Response* Unwrap() const {
        ZX_DEBUG_ASSERT(ok());
        return reinterpret_cast<const {{ .Name }}Response*>(bytes_);
      }

      {{ .Name }}Response& value() { return *Unwrap(); }
      const {{ .Name }}Response& value() const { return *Unwrap(); }

      {{ .Name }}Response* operator->() { return &value(); }
      const {{ .Name }}Response* operator->() const { return &value(); }

      {{ .Name }}Response& operator*() { return value(); }
      const {{ .Name }}Response& operator*() const { return value(); }

     private:
      uint8_t* bytes_;
      {{- end }}
    };
    {{- end }}
  };

  {{ range .Methods }}
    {{ if .HasResponse }}

  class {{ .Name }}UnownedResponse final {
   public:
    {{ .Name }}UnownedResponse(uint8_t* _bytes, uint32_t _byte_size
      {{- template "CommaMessagePrototype" .Response }})
        : message_(_bytes, _byte_size, sizeof({{ .Name }}Response),
    {{- if gt .ResponseMaxHandles 0 }}
      handles_, std::min(ZX_CHANNEL_MAX_MSG_HANDLES, {{ .Name }}Response::MaxNumHandles), 0
    {{- else }}
      nullptr, 0, 0
    {{- end }}
      ) {
  {{- if .LLProps.LinearizeResponse }}
  {{/* tracking_ptr destructors will be called when _response goes out of scope */}}
        FIDL_ALIGNDECL {{ .Name }}Response _response({{- template "PassthroughMessageParams" .Response }});
  {{- else }}
  {{/* tracking_ptrs won't free allocated memory because destructors aren't called.
  This is ok because there are no tracking_ptrs, since LinearizeResponse is true when
  there are pointers in the object. */}}
        // Destructors can't be called because it will lead to handle double close
        // (here and in fidl::Encode).
        FIDL_ALIGNDECL uint8_t _response_buffer[sizeof({{ .Name }}Response)];
        auto& _response = *new (_response_buffer) {{ .Name }}Response(
        {{- template "PassthroughMessageParams" .Response -}}
        );
  {{- end }}
        message_.LinearizeAndEncode({{ .Name }}Response::Type, &_response);
      }

    zx_status_t status() const { return message_.status(); }
    bool ok() const { return message_.status() == ZX_OK; }
    const char* error() const { return message_.error(); }
    bool linearized() const { return message_.linearized(); }
    bool encoded() const { return message_.encoded(); }

    ::fidl::internal::FidlMessage& GetFidlMessage() { return message_; }

    void Write(zx_handle_t client) { message_.Write(client); }

   private:
    {{ .Name }}Response& Message() { return *reinterpret_cast<{{ .Name }}Response*>(message_.bytes().data()); }

    {{- if gt .ResponseMaxHandles 0 }}
      zx_handle_t handles_[std::min(ZX_CHANNEL_MAX_MSG_HANDLES, {{ .Name }}Response::MaxNumHandles)];
    {{- end }}
    ::fidl::internal::FidlMessage message_;
  };

  class {{ .Name }}OwnedResponse final {
   public:
    explicit {{ .Name }}OwnedResponse(
      {{- template "MessagePrototype" .Response }})
        {{- if gt .ResponseSentMaxSize 512 -}}
      : bytes_(std::make_unique<::fidl::internal::AlignedBuffer<{{- template "ResponseSentSize" .}}>>()),
        message_(bytes_->data(), {{- template "ResponseSentSize" .}}
        {{- else }}
        : message_(bytes_, sizeof(bytes_)
        {{- end }}
        {{- template "CommaPassthroughMessageParams" .Response }}) {}

    zx_status_t status() const { return message_.status(); }
    bool ok() const { return message_.ok(); }
    const char* error() const { return message_.error(); }
    bool linearized() const { return message_.linearized(); }
    bool encoded() const { return message_.encoded(); }

    ::fidl::internal::FidlMessage& GetFidlMessage() { return message_.GetFidlMessage(); }

    void Write(zx_handle_t client) { message_.Write(client); }

   private:
    {{- if gt .ResponseSentMaxSize 512 }}
    std::unique_ptr<::fidl::internal::AlignedBuffer<{{- template "ResponseSentSize" .}}>> bytes_;
    {{- else }}
    FIDL_ALIGNDECL
    uint8_t bytes_[{{ .Name }}Response::PrimarySize + {{ .Name }}Response::MaxOutOfLine];
    {{- end }}
    {{ .Name }}UnownedResponse message_;
  };
    {{ end }}
    {{ if .HasRequest }}

  class {{ .Name }}UnownedRequest final {
   public:
    {{ .Name }}UnownedRequest(uint8_t* _bytes, uint32_t _byte_size, zx_txid_t _txid
      {{- template "CommaMessagePrototype" .Request }})
        : message_(_bytes, _byte_size, sizeof({{ .Name }}Request),
    {{- if gt .RequestMaxHandles 0 }}
      handles_, std::min(ZX_CHANNEL_MAX_MSG_HANDLES, {{ .Name }}Request::MaxNumHandles), 0
    {{- else }}
      nullptr, 0, 0
    {{- end }}
      ) {
  {{- if .LLProps.LinearizeRequest }}
  {{/* tracking_ptr destructors will be called when _response goes out of scope */}}
        FIDL_ALIGNDECL {{ .Name }}Request _request(_txid  {{- template "CommaPassthroughMessageParams" .Request }});
  {{- else }}
  {{/* tracking_ptrs won't free allocated memory because destructors aren't called.
  This is ok because there are no tracking_ptrs, since LinearizeResponse is true when
  there are pointers in the object. */}}
        // Destructors can't be called because it will lead to handle double close
        // (here and in fidl::Encode).
        FIDL_ALIGNDECL uint8_t _request_buffer[sizeof({{ .Name }}Request)];
        auto& _request = *new (_request_buffer) {{ .Name }}Request(_txid
        {{- template "CommaPassthroughMessageParams" .Request -}}
        );
  {{- end }}
        message_.LinearizeAndEncode({{ .Name }}Request::Type, &_request);
      }

    zx_status_t status() const { return message_.status(); }
    bool ok() const { return message_.status() == ZX_OK; }
    const char* error() const { return message_.error(); }
    bool linearized() const { return message_.linearized(); }
    bool encoded() const { return message_.encoded(); }

    ::fidl::internal::FidlMessage& GetFidlMessage() { return message_; }

    void Write(zx_handle_t client) { message_.Write(client); }

   private:
    {{ .Name }}Request& Message() { return *reinterpret_cast<{{ .Name }}Request*>(message_.bytes().data()); }

    {{- if gt .RequestMaxHandles 0 }}
      zx_handle_t handles_[std::min(ZX_CHANNEL_MAX_MSG_HANDLES, {{ .Name }}Request::MaxNumHandles)];
    {{- end }}
    ::fidl::internal::FidlMessage message_;
  };

  class {{ .Name }}OwnedRequest final {
   public:
    explicit {{ .Name }}OwnedRequest(zx_txid_t _txid
      {{- template "CommaMessagePrototype" .Request }})
        {{- if gt .RequestSentMaxSize 512 -}}
      : bytes_(std::make_unique<::fidl::internal::AlignedBuffer<{{- template "RequestSentSize" .}}>>()),
        message_(bytes_->data(), {{- template "RequestSentSize" .}}, _txid
        {{- else }}
        : message_(bytes_, sizeof(bytes_), _txid
        {{- end }}
        {{- template "CommaPassthroughMessageParams" .Request }}) {}

    zx_status_t status() const { return message_.status(); }
    bool ok() const { return message_.ok(); }
    const char* error() const { return message_.error(); }
    bool linearized() const { return message_.linearized(); }
    bool encoded() const { return message_.encoded(); }

    ::fidl::internal::FidlMessage& GetFidlMessage() { return message_.GetFidlMessage(); }

    void Write(zx_handle_t client) { message_.Write(client); }

   private:
    {{- if gt .RequestSentMaxSize 512 }}
    std::unique_ptr<::fidl::internal::AlignedBuffer<{{- template "RequestSentSize" .}}>> bytes_;
    {{- else }}
    FIDL_ALIGNDECL
    uint8_t bytes_[{{ .Name }}Request::PrimarySize + {{ .Name }}Request::MaxOutOfLine];
    {{- end }}
    {{ .Name }}UnownedRequest message_;
  };
    {{ end }}
  {{ end }}

  class SyncClient final {
   public:
    SyncClient() = default;
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

{{ template "ClientForwardDeclaration" . }}

{{ "" }}
  // Pure-virtual interface to be implemented by a server.
  class Interface {
   public:
    Interface() = default;
    virtual ~Interface() = default;
    using _Outer = {{ $protocol.Name }};
    using _Base = ::fidl::CompleterBase;
{{ "" }}
    {{- range .Methods }}
      {{- if .HasRequest }}
        {{- if .HasResponse }}
    class {{ .Name }}CompleterBase : public _Base {
     public:
      // In the following methods, the return value indicates internal errors during
      // the reply, such as encoding or writing to the transport.
      // Note that any error will automatically lead to the destruction of the binding,
      // after which the |on_unbound| callback will be triggered with a detailed reason.
      //
      // See //zircon/system/ulib/fidl/include/lib/fidl/llcpp/server.h.
      //
      // Because the reply status is identical to the unbinding status, it can be safely ignored.
      ::fidl::Result {{ template "ReplyCFlavorMethodSignature" . }};
          {{- if .Result }}
      ::fidl::Result {{ template "ReplyCFlavorResultSuccessMethodSignature" . }};
      ::fidl::Result {{ template "ReplyCFlavorResultErrorMethodSignature" . }};
          {{- end }}
          {{- if .Response }}
      ::fidl::Result {{ template "ReplyCallerAllocateMethodSignature" . }};
            {{- if .Result }}
      ::fidl::Result {{ template "ReplyCallerAllocateResultSuccessMethodSignature" . }};
            {{- end }}
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

  // Dispatches the incoming message to one of the handlers functions in the protocol.
  // If there is no matching handler, it closes all the handles in |msg| and closes the channel with
  // a |ZX_ERR_NOT_SUPPORTED| epitaph, before returning false. The message should then be discarded.
  static bool Dispatch{{ template "SyncServerDispatchMethodSignature" }};

  // Same as |Dispatch|, but takes a |void*| instead of |Interface*|.
  // Only used with |fidl::BindServer| to reduce template expansion.
  // Do not call this method manually. Use |Dispatch| instead.
  static bool TypeErasedDispatch(void* impl, fidl_msg_t* msg, ::fidl::Transaction* txn) {
    return Dispatch(static_cast<Interface*>(impl), msg, txn);
  }

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

  class EventSender;
};
{{- end }}

{{- define "ProtocolTraits" -}}
{{ $protocol := . -}}
{{ range .Methods -}}
{{ $method := . -}}
{{- if .HasRequest }}

template <>
struct IsFidlType<{{ $protocol.Namespace }}::{{ $protocol.Name }}::{{ .Name }}Request> : public std::true_type {};
template <>
struct IsFidlMessage<{{ $protocol.Namespace }}::{{ $protocol.Name }}::{{ .Name }}Request> : public std::true_type {};
static_assert(sizeof({{ $protocol.Namespace }}::{{ $protocol.Name }}::{{ .Name }}Request)
    == {{ $protocol.Namespace }}::{{ $protocol.Name }}::{{ .Name }}Request::PrimarySize);
{{- range $index, $param := .Request }}
static_assert(offsetof({{ $protocol.Namespace }}::{{ $protocol.Name }}::{{ $method.Name }}Request, {{ $param.Name }}) == {{ $param.Offset }});
{{- end }}
{{- end }}
{{- if .HasResponse }}

template <>
struct IsFidlType<{{ $protocol.Namespace }}::{{ $protocol.Name }}::{{ .Name }}Response> : public std::true_type {};
template <>
struct IsFidlMessage<{{ $protocol.Namespace }}::{{ $protocol.Name }}::{{ .Name }}Response> : public std::true_type {};
static_assert(sizeof({{ $protocol.Namespace }}::{{ $protocol.Name }}::{{ .Name }}Response)
    == {{ $protocol.Namespace }}::{{ $protocol.Name }}::{{ .Name }}Response::PrimarySize);
{{- range $index, $param := .Response }}
static_assert(offsetof({{ $protocol.Namespace }}::{{ $protocol.Name }}::{{ $method.Name }}Response, {{ $param.Name }}) == {{ $param.Offset }});
{{- end }}
{{- end }}
{{- end }}
{{- end }}

{{- define "ReturnResponseStructMembers" }}
{{- range $param := . }}
  *out_{{ $param.Name }} = std::move(_response.{{ $param.Name }});
{{- end }}
{{- end }}

{{- define "ProtocolDefinition" }}

namespace {
{{ $protocol := . -}}

{{- range .Methods }}
[[maybe_unused]]
constexpr uint64_t {{ .OrdinalName }} = {{ .Ordinal }}lu;
extern "C" const fidl_type_t {{ .RequestTypeName }};
extern "C" const fidl_type_t {{ .ResponseTypeName }};
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
{{- end }}

{{- range FilterMethodsWithoutReqs .Methods }}
{{ "" }}
  {{- template "ClientSyncRequestManagedMethodDefinition" . }}
  {{- if or .Request .Response }}
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

{{- if .HasEvents }}
  {{- template "SyncEventHandlerMethodDefinition" . }}
{{ "" }}
  {{- template "StaticCallSyncEventHandlerMethodDefinition" . }}
{{- end }}

{{- /* Server implementation */}}
{{ template "SyncServerTryDispatchMethodDefinition" . }}
{{ template "SyncServerDispatchMethodDefinition" . }}

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
      {{- end }}
{{ "" }}
    {{- end }}
  {{- end }}
{{ "" }}

  {{- range .Methods }}
{{ "" }}
    {{- if .HasRequest }}
{{ "" }}
    void {{ .LLProps.ProtocolName }}::{{ .Name }}Request::_InitHeader(zx_txid_t _txid) {
      fidl_init_txn_header(&_hdr, _txid, {{ .OrdinalName }});
    }
    {{- end }}
    {{- if .HasResponse }}
{{ "" }}
    void {{ .LLProps.ProtocolName }}::{{ .Name }}Response::_InitHeader() {
      fidl_init_txn_header(&_hdr, 0, {{ .OrdinalName }});
    }
    {{- end }}
  {{- end }}
{{- end }}

{{- end }}
`
