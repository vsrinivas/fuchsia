// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentMethodResponseTmpl = `
{{- define "MethodResponseDeclaration" }}
{{- EnsureNamespace "" }}
template<>
struct {{ .WireResponse }} final {
  FIDL_ALIGNDECL
    {{- /* Add underscore to prevent name collision */}}
  fidl_message_header_t _hdr;
    {{- range $index, $param := .ResponseArgs }}
  {{ $param.Type }} {{ $param.Name }};
    {{- end }}

  {{- if .ResponseArgs }}
  explicit {{ .WireResponse.Self }}({{ .ResponseArgs | CalleeParams }})
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
    {{- .ResponseArgs | CalleeCommaParams }})
    : message_(_bytes, _byte_size, sizeof({{ .WireResponse.Self }}),
  {{- if gt .Response.MaxHandles 0 }}
    handles_, std::min(ZX_CHANNEL_MAX_MSG_HANDLES, MaxNumHandles), 0
  {{- else }}
    nullptr, 0, 0
  {{- end }}
    ) {
    FIDL_ALIGNDECL {{ .WireResponse.Self }} _response{
    {{- .ResponseArgs | ForwardParams -}}
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
  {{- if gt .Response.MaxHandles 0 }}
    zx_handle_disposition_t handles_[std::min(ZX_CHANNEL_MAX_MSG_HANDLES, MaxNumHandles)];
  {{- end }}
  ::fidl::OutgoingMessage message_;
  };

  class OwnedEncodedMessage final {
   public:
  explicit OwnedEncodedMessage({{ .ResponseArgs | CalleeParams }})
    : message_(bytes_.data(), bytes_.size()
    {{- .ResponseArgs | ForwardCommaParams }}) {}
  explicit OwnedEncodedMessage({{ .WireResponse }}* response)
    : message_(bytes_.data(), bytes_.size(), response) {}
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




{{- define "MethodResponseDefinition" }}
  {{- EnsureNamespace "" }}
  void {{ .WireResponse }}::_InitHeader() {
    fidl_init_txn_header(&_hdr, 0, {{ .OrdinalName }});
  }

  {{ if .Response.IsResource }}
    void {{ .WireResponse }}::_CloseHandles() {
      {{- range .ResponseArgs }}
        {{- CloseHandles . false false }}
      {{- end }}
    }
  {{- end }}
{{- end }}
`
