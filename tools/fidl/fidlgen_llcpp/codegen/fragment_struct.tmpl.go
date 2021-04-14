// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentStructTmpl = `
{{- define "StructForwardDeclaration" }}
{{ EnsureNamespace . }}
struct {{ .Name }};
{{- end }}

{{/* TODO(fxbug.dev/36441): Remove __Fuchsia__ ifdefs once we have non-Fuchsia
     emulated handles for C++. */}}
{{- define "StructDeclaration" }}
{{ EnsureNamespace . }}
{{ if .IsResourceType }}
{{- IfdefFuchsia -}}
{{- end }}
extern "C" const fidl_type_t {{ .CodingTableType }};
{{ .Docs }}
struct {{ .Name }} {
  static constexpr const fidl_type_t* Type = &{{ .CodingTableType }};
  static constexpr uint32_t MaxNumHandles = {{ .MaxHandles }};
  static constexpr uint32_t PrimarySize = {{ .InlineSize }};
  [[maybe_unused]]
  static constexpr uint32_t MaxOutOfLine = {{ .MaxOutOfLine }};
  static constexpr bool HasPointer = {{ .HasPointer }};

  {{- range .Members }}
{{ "" }}
    {{- .Docs }}
    {{ .Type }} {{ .Name }} = {};
  {{- end }}

  {{- if .IsResourceType }}

  void _CloseHandles();
  {{- end }}

  class UnownedEncodedMessage final {
   public:
    UnownedEncodedMessage(uint8_t* backing_buffer, uint32_t backing_buffer_size, {{ .Name }}* value)
      : message_(::fidl::OutgoingMessage::ConstructorArgs{
          .iovecs = iovecs_,
          .iovec_capacity = ::fidl::internal::IovecBufferSize,
    {{- if gt .MaxHandles 0 }}
          .handles = handles_,
          .handle_capacity = std::min(ZX_CHANNEL_MAX_MSG_HANDLES, MaxNumHandles),
    {{- end }}
          .backing_buffer = backing_buffer,
          .backing_buffer_capacity = backing_buffer_size,
        }) {
      if (backing_buffer_size < sizeof({{ .Name }})) {
        ::fidl::internal::OutgoingMessageResultSetter::SetResult(
          message_, ZX_ERR_BUFFER_TOO_SMALL, nullptr);
        return;
      }
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
    const char* error() const { return message_.error(); }

    ::fidl::OutgoingMessage& GetOutgoingMessage() { return message_; }

   private:
    ::fidl::internal::IovecBuffer iovecs_;
    {{- if gt .MaxHandles 0 }}
      zx_handle_disposition_t handles_[std::min(ZX_CHANNEL_MAX_MSG_HANDLES, MaxNumHandles)];
    {{- end }}
    ::fidl::OutgoingMessage message_;
  };

  class OwnedEncodedMessage final {
   public:
    explicit OwnedEncodedMessage({{ .Name }}* value)
      : message_(backing_buffer_.data(), backing_buffer_.size(), value) {}
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

   private:
    {{ .BackingBufferType }} backing_buffer_;
    UnownedEncodedMessage message_;
  };

  class DecodedMessage final : public ::fidl::internal::IncomingMessage {
   public:
    DecodedMessage(uint8_t* bytes, uint32_t byte_actual, zx_handle_info_t* handles = nullptr,
                    uint32_t handle_actual = 0)
        : ::fidl::internal::IncomingMessage(bytes, byte_actual, handles, handle_actual) {
      Decode<struct {{ .Name }}>();
    }
    DecodedMessage(fidl_incoming_msg_t* msg) : ::fidl::internal::IncomingMessage(msg) {
      Decode<struct {{ .Name }}>();
    }
    DecodedMessage(const DecodedMessage&) = delete;
    DecodedMessage(DecodedMessage&&) = delete;
    DecodedMessage* operator=(const DecodedMessage&) = delete;
    DecodedMessage* operator=(DecodedMessage&&) = delete;
    {{- if .IsResourceType }}
    ~DecodedMessage() {
      if (ok() && (PrimaryObject() != nullptr)) {
        PrimaryObject()->_CloseHandles();
      }
    }
    {{- end }}

    struct {{ .Name }}* PrimaryObject() {
      ZX_DEBUG_ASSERT(ok());
      return reinterpret_cast<struct {{ .Name }}*>(bytes());
    }

    // Release the ownership of the decoded message. That means that the handles won't be closed
    // When the object is destroyed.
    // After calling this method, the DecodedMessage object should not be used anymore.
    void ReleasePrimaryObject() { ResetBytes(); }
  };
};
{{- if .IsResourceType }}
{{- EndifFuchsia -}}
{{- end }}
{{- end }}

{{/* TODO(fxbug.dev/36441): Remove __Fuchsia__ ifdefs once we have non-Fuchsia
     emulated handles for C++. */}}
{{- define "StructDefinition" }}
{{ EnsureNamespace "::" }}
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
{{- define "StructTraits" }}
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
static_assert(offsetof({{ $struct }}, {{ .Name }}) == {{ .Offset }});
{{- end }}
static_assert(sizeof({{ . }}) == {{ . }}::PrimarySize);
{{- if .IsResourceType }}
{{- EndifFuchsia -}}
{{- end }}
{{- end }}
`
