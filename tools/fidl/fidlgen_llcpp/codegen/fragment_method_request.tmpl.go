// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentMethodRequestTmpl = `
{{- define "MethodRequestDeclaration" }}
{{- EnsureNamespace "" }}
template<>
struct {{ .WireRequest }} final {
  FIDL_ALIGNDECL
  {{- /* Add underscore to prevent name collision */}}
  fidl_message_header_t _hdr;
    {{- range $index, $param := .RequestArgs }}
  {{ $param.Type }} {{ $param.Name }};
    {{- end }}

  {{- if .RequestArgs }}
  explicit {{ .WireRequest.Self }}(zx_txid_t _txid {{- .RequestArgs | CalleeCommaParams }})
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
  UnownedEncodedMessage(uint8_t* _backing_buffer, uint32_t _backing_buffer_size, zx_txid_t _txid
    {{- .RequestArgs | CalleeCommaParams }})
    : message_(::fidl::OutgoingMessage::ConstructorArgs{
        .iovecs = iovecs_,
        .iovec_capacity = ::fidl::internal::IovecBufferSize,
  {{- if gt .Request.MaxHandles 0 }}
        .handles = handles_,
        .handle_capacity = std::min(ZX_CHANNEL_MAX_MSG_HANDLES, MaxNumHandles),
  {{- end }}
        .backing_buffer = _backing_buffer,
        .backing_buffer_capacity = _backing_buffer_size,
      }) {
    if (_backing_buffer_size < sizeof({{ .WireRequest.Self }})) {
      ::fidl::internal::OutgoingMessageResultSetter::SetResult(
        message_, ZX_ERR_BUFFER_TOO_SMALL, nullptr);
      return;
    }
    FIDL_ALIGNDECL {{ .WireRequest.Self }} _request(_txid
      {{- .RequestArgs | ForwardCommaParams -}}
    );
    message_.Encode<{{ .WireRequest.Self }}>(&_request);
  }
  UnownedEncodedMessage(uint8_t* _backing_buffer, uint32_t _backing_buffer_size,
                        {{ .WireRequest.Self }}* request)
    : message_(::fidl::OutgoingMessage::ConstructorArgs{
        .iovecs = iovecs_,
        .iovec_capacity = ::fidl::internal::IovecBufferSize,
  {{- if gt .Request.MaxHandles 0 }}
        .handles = handles_,
        .handle_capacity = std::min(ZX_CHANNEL_MAX_MSG_HANDLES, MaxNumHandles),
  {{- end }}
        .backing_buffer = _backing_buffer,
        .backing_buffer_capacity = _backing_buffer_size,
      }) {
    if (_backing_buffer_size < sizeof({{ .WireRequest.Self }})) {
      ::fidl::internal::OutgoingMessageResultSetter::SetResult(
        message_, ZX_ERR_BUFFER_TOO_SMALL, nullptr);
      return;
    }
    message_.Encode<{{ .WireRequest.Self }}>(request);
  }
  UnownedEncodedMessage(const UnownedEncodedMessage&) = delete;
  UnownedEncodedMessage(UnownedEncodedMessage&&) = delete;
  UnownedEncodedMessage* operator=(const UnownedEncodedMessage&) = delete;
  UnownedEncodedMessage* operator=(UnownedEncodedMessage&&) = delete;

  zx_status_t status() const { return message_.status(); }
  {{- IfdefFuchsia -}}
  const char* status_string() const { return message_.status_string(); }
  {{- EndifFuchsia -}}
  bool ok() const { return message_.status() == ZX_OK; }
  const char* error() const { return message_.error(); }

  ::fidl::OutgoingMessage& GetOutgoingMessage() { return message_; }

  {{- IfdefFuchsia -}}
  template <typename ChannelLike>
  void Write(ChannelLike&& client) { message_.Write(std::forward<ChannelLike>(client)); }
  {{- EndifFuchsia -}}

  private:
  ::fidl::internal::IovecBuffer iovecs_;
  {{- if gt .Request.MaxHandles 0 }}
    zx_handle_disposition_t handles_[std::min(ZX_CHANNEL_MAX_MSG_HANDLES, MaxNumHandles)];
  {{- end }}
  ::fidl::OutgoingMessage message_;
  };

  class OwnedEncodedMessage final {
  public:
    explicit OwnedEncodedMessage(zx_txid_t _txid
      {{- .RequestArgs | CalleeCommaParams }})
      : message_(backing_buffer_.data(), backing_buffer_.size(), _txid
      {{- .RequestArgs | ForwardCommaParams }}) {}
    explicit OwnedEncodedMessage({{ .WireRequest.Self }}* request)
      : message_(backing_buffer_.data(), backing_buffer_.size(), request) {}
    OwnedEncodedMessage(const OwnedEncodedMessage&) = delete;
    OwnedEncodedMessage(OwnedEncodedMessage&&) = delete;
    OwnedEncodedMessage* operator=(const OwnedEncodedMessage&) = delete;
    OwnedEncodedMessage* operator=(OwnedEncodedMessage&&) = delete;

    zx_status_t status() const { return message_.status(); }
    {{- IfdefFuchsia -}}
    const char* status_string() const { return message_.status_string(); }
    {{- EndifFuchsia -}}
    bool ok() const { return message_.ok(); }
    const char* error() const { return message_.error(); }

    ::fidl::OutgoingMessage& GetOutgoingMessage() { return message_.GetOutgoingMessage(); }

    {{- IfdefFuchsia -}}
    template <typename ChannelLike>
    void Write(ChannelLike&& client) { message_.Write(std::forward<ChannelLike>(client)); }
    {{- EndifFuchsia -}}

  private:
    {{ .Request.ClientAllocation.BackingBufferType }} backing_buffer_;
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
{{- end }}




{{- define "MethodRequestDefinition" }}
  {{- EnsureNamespace "" }}

  void {{ .WireRequest }}::_InitHeader(zx_txid_t _txid) {
    fidl_init_txn_header(&_hdr, _txid, {{ .OrdinalName }});
  }

  {{ if .Request.IsResource }}
    void {{ .WireRequest }}::_CloseHandles() {
      {{- range .RequestArgs }}
        {{- CloseHandles . false false }}
      {{- end }}
    }
  {{- end }}
{{- end }}
`
