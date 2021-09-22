// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentStructTmpl = `
{{- define "Struct:ForwardDeclaration:Header" }}
{{ EnsureNamespace . }}
struct {{ .Name }};
{{- end }}

{{/* TODO(fxbug.dev/36441): Remove __Fuchsia__ ifdefs once we have non-Fuchsia
     emulated handles for C++. */}}
{{- define "Struct:Header" }}
{{ EnsureNamespace . }}
{{ if .IsResourceType }}
{{- IfdefFuchsia -}}
{{- end }}
extern "C" const fidl_type_t {{ .CodingTableType }};
{{ .Docs }}
struct {{ .Name }} {
  static constexpr const fidl_type_t* Type = &{{ .CodingTableType }};
  static constexpr uint32_t MaxNumHandles = {{ .TypeShapeV2.MaxHandles }};
  static constexpr uint32_t PrimarySize = {{ .TypeShapeV2.InlineSize }};
  [[maybe_unused]]
  static constexpr uint32_t MaxOutOfLine = {{ .TypeShapeV2.MaxOutOfLine }};
  static constexpr bool HasPointer = {{ .TypeShapeV2.HasPointer }};

{{- range .AnonymousChildren }}
  using {{ .ScopedName }} = {{ .FlattenedName }};
{{- end }}

  {{- range .Members }}
{{ "" }}
    {{- .Docs }}
    {{ .Type }} {{ .Name }} = {};
  {{- end }}

  {{- if .IsResourceType }}

  void _CloseHandles();
  {{- end }}

  class UnownedEncodedMessage;
  class OwnedEncodedMessage;
  class DecodedMessage;
};

class {{ .Name }}::UnownedEncodedMessage final {
  public:
  UnownedEncodedMessage(uint8_t* backing_buffer, uint32_t backing_buffer_size, {{ .Name }}* value)
    : UnownedEncodedMessage(::fidl::internal::IovecBufferSize, backing_buffer, backing_buffer_size, value) {}
  UnownedEncodedMessage(uint32_t iovec_capacity, uint8_t* backing_buffer, uint32_t backing_buffer_size,
    {{ .Name }}* value)
    : message_(::fidl::OutgoingMessage::ConstructorArgs{
        .iovecs = iovecs_,
        .iovec_capacity = iovec_capacity,
  {{- if gt .TypeShapeV1.MaxHandles 0 }}
        .handles = handles_,
        .handle_capacity = std::min(ZX_CHANNEL_MAX_MSG_HANDLES, MaxNumHandles),
  {{- end }}
        .backing_buffer = backing_buffer,
        .backing_buffer_capacity = backing_buffer_size,
      }) {
    ZX_ASSERT(iovec_capacity <= std::size(iovecs_));
    message_.Encode<{{ .Name }}>(value);
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
  std::string FormatDescription() const { return message_.FormatDescription(); }
  const char* lossy_description() const { return message_.lossy_description(); }
  const ::fidl::Result& error() const { return message_.error(); }

  ::fidl::OutgoingMessage& GetOutgoingMessage() { return message_; }

  private:
  ::fidl::internal::IovecBuffer iovecs_;
  {{- if gt .TypeShapeV1.MaxHandles 0 }}
    zx_handle_disposition_t handles_[std::min(ZX_CHANNEL_MAX_MSG_HANDLES, MaxNumHandles)];
  {{- end }}
  ::fidl::OutgoingMessage message_;
};

class {{ .Name }}::OwnedEncodedMessage final {
  public:
  explicit OwnedEncodedMessage({{ .Name }}* value)
    : message_(1u, backing_buffer_.data(), static_cast<uint32_t>(backing_buffer_.size()), value) {}
  // Internal constructor.
  explicit OwnedEncodedMessage(::fidl::internal::AllowUnownedInputRef allow_unowned, {{ .Name }}* value)
    : message_(::fidl::internal::IovecBufferSize, backing_buffer_.data(), static_cast<uint32_t>(backing_buffer_.size()), value) {}
  OwnedEncodedMessage(const OwnedEncodedMessage&) = delete;
  OwnedEncodedMessage(OwnedEncodedMessage&&) = delete;
  OwnedEncodedMessage* operator=(const OwnedEncodedMessage&) = delete;
  OwnedEncodedMessage* operator=(OwnedEncodedMessage&&) = delete;

  zx_status_t status() const { return message_.status(); }
{{- IfdefFuchsia -}}
  const char* status_string() const { return message_.status_string(); }
{{- EndifFuchsia -}}
  bool ok() const { return message_.ok(); }
  std::string FormatDescription() const { return message_.FormatDescription(); }
  const char* lossy_description() const { return message_.lossy_description(); }
  const ::fidl::Result& error() const { return message_.error(); }

  ::fidl::OutgoingMessage& GetOutgoingMessage() { return message_.GetOutgoingMessage(); }

  private:
  {{ .BackingBufferTypeV1 }} backing_buffer_;
  UnownedEncodedMessage message_;
};

class {{ .Name }}::DecodedMessage final : public ::fidl::internal::DecodedMessageBase<{{ .Name }}> {
  public:
  using DecodedMessageBase<{{ .Name }}>::DecodedMessageBase;

  DecodedMessage(uint8_t* bytes, uint32_t byte_actual, zx_handle_info_t* handles = nullptr,
                  uint32_t handle_actual = 0)
      : DecodedMessageBase(
            ::fidl::internal::kLLCPPEncodedWireFormatVersion,
            ::fidl::IncomingMessage(bytes, byte_actual, handles, handle_actual,
                ::fidl::IncomingMessage::kSkipMessageHeaderValidation)) {}

  // Internal constructor for specifying a specific wire format version.
  DecodedMessage(::fidl::internal::WireFormatVersion wire_format_version,
          uint8_t* bytes, uint32_t byte_actual, zx_handle_info_t* handles = nullptr,
                  uint32_t handle_actual = 0)
      : DecodedMessageBase(
            wire_format_version,
            ::fidl::IncomingMessage(bytes, byte_actual, handles, handle_actual,
                ::fidl::IncomingMessage::kSkipMessageHeaderValidation)) {}

  DecodedMessage(const fidl_incoming_msg_t* c_msg)
      : DecodedMessage(reinterpret_cast<uint8_t*>(c_msg->bytes), c_msg->num_bytes,
                        c_msg->handles, c_msg->num_handles) {}

  // Internal constructor for specifying a specific wire format version.
  DecodedMessage(::fidl::internal::WireFormatVersion wire_format_version,
                  const fidl_incoming_msg_t* c_msg)
      : DecodedMessage(wire_format_version, reinterpret_cast<uint8_t*>(c_msg->bytes),
                        c_msg->num_bytes, c_msg->handles, c_msg->num_handles) {}

  {{- if .IsResourceType }}
  ~DecodedMessage() {
    if (ok() && (PrimaryObject() != nullptr)) {
      PrimaryObject()->_CloseHandles();
    }
  }
  {{- end }}

  {{ .Name }}* PrimaryObject() {
    ZX_DEBUG_ASSERT(ok());
    return reinterpret_cast<{{ .Name }}*>(bytes());
  }

  // Release the ownership of the decoded message. That means that the handles won't be closed
  // When the object is destroyed.
  // After calling this method, the |DecodedMessage| object should not be used anymore.
  void ReleasePrimaryObject() { ResetBytes(); }
};

{{- if .IsResourceType }}
{{- EndifFuchsia -}}
{{- end }}
{{- end }}

{{/* TODO(fxbug.dev/36441): Remove __Fuchsia__ ifdefs once we have non-Fuchsia
     emulated handles for C++. */}}
{{- define "Struct:Source" }}
{{ EnsureNamespace "" }}
{{ if .IsResourceType }}
{{- IfdefFuchsia -}}
void {{ . }}::_CloseHandles() {
  {{- range .Members }}
    {{- CloseHandles . false false }}
  {{- end }}
}
{{- EndifFuchsia -}}
{{- end }}
{{- end }}

{{/* TODO(fxbug.dev/36441): Remove __Fuchsia__ ifdefs once we have non-Fuchsia
     emulated handles for C++. */}}
{{- define "Struct:Traits:Header" }}
{{ if .IsResourceType }}
{{- IfdefFuchsia -}}
{{- end }}
template <>
struct IsFidlType<{{ . }}> : public std::true_type {};
template <>
struct IsStruct<{{ . }}> : public std::true_type {};
static_assert(std::is_standard_layout_v<{{ . }}>);
{{- $struct := . }}
{{- range .Members }}
static_assert(offsetof({{ $struct }}, {{ .Name }}) == {{ .OffsetV2 }});
{{- end }}
static_assert(sizeof({{ . }}) == {{ . }}::PrimarySize);
{{- if .IsResourceType }}
{{- EndifFuchsia -}}
{{- end }}
{{- end }}
`
