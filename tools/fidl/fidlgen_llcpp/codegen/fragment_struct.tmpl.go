// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentStructTmpl = `
{{- define "StructForwardDeclaration" }}
struct {{ .Name }};
{{- end }}

{{- define "StructMemberCloseHandles" }}
  {{- if .Type.IsResource }}
    {{- template "TypeCloseHandles" NewTypedArgument .Name .Type .Type.LLPointer false false }}
  {{- else if .Type.ExternalDeclaration }}
  if constexpr ({{ .Type.LLClass }}::IsResource) {
    {{- template "TypeCloseHandles" NewTypedArgument .Name .Type .Type.LLPointer false false }}
  }
  {{- end }}
{{- end }}

{{- define "SentSize"}}
  {{- if gt .MaxSentSize 65536 -}}
  ZX_CHANNEL_MAX_MSG_BYTES
  {{- else -}}
  FIDL_ALIGN(PrimarySize + MaxOutOfLine)
  {{- end -}}
{{- end }}

{{/* TODO(fxbug.dev/36441): Remove __Fuchsia__ ifdefs once we have non-Fuchsia
     emulated handles for C++. */}}
{{- define "StructDeclaration" }}
{{ if .IsResource }}
#ifdef __Fuchsia__
{{- end }}
extern "C" const fidl_type_t {{ .TableType }};
{{range .DocComments}}
//{{ . }}
{{- end}}
struct {{ .Name }} {
  static constexpr const fidl_type_t* Type = &{{ .TableType }};
  static constexpr uint32_t MaxNumHandles = {{ .MaxHandles }};
  static constexpr uint32_t PrimarySize = {{ .InlineSize }};
  [[maybe_unused]]
  static constexpr uint32_t MaxOutOfLine = {{ .MaxOutOfLine }};
  static constexpr bool HasPointer = {{ .HasPointer }};
  static constexpr bool IsResource = {{ .IsResource }};

  {{- range .Members }}
{{ "" }}
  {{- range .DocComments }}
  //{{ . }}
  {{- end }}
  {{ .Type.LLDecl }} {{ .Name }} = {};
  {{- end }}
  void _CloseHandles();

  // TODO(fxbug.dev/62485): rename to UnownedEncodedMessage.
  class UnownedEncodedMessage final {
   public:
    UnownedEncodedMessage(uint8_t* bytes, uint32_t byte_size, {{ .Name }}* value)
        : message_(bytes, byte_size, sizeof({{ .Name }}),
    {{- if gt .MaxHandles 0 }}
      handles_, std::min(ZX_CHANNEL_MAX_MSG_HANDLES, MaxNumHandles), 0
    {{- else }}
      nullptr, 0, 0
    {{- end }}
      ) {
      message_.LinearizeAndEncode<{{ .Name }}>(value);
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

   private:
    {{- if gt .MaxHandles 0 }}
      zx_handle_t handles_[std::min(ZX_CHANNEL_MAX_MSG_HANDLES, MaxNumHandles)];
    {{- end }}
    ::fidl::OutgoingMessage message_;
  };

  // TODO(fxbug.dev/62485): rename to OwnedEncodedMessage.
  class OwnedEncodedMessage final {
   public:
    explicit OwnedEncodedMessage({{ .Name }}* value)
        {{- if gt .MaxSentSize 512 -}}
      : bytes_(std::make_unique<::fidl::internal::AlignedBuffer<{{- template "SentSize" .}}>>()),
        message_(bytes_->data(), {{- template "SentSize" .}}
        {{- else }}
        : message_(bytes_, sizeof(bytes_)
        {{- end }}
        , value) {}
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

   private:
    {{- if gt .MaxSentSize 512 }}
    std::unique_ptr<::fidl::internal::AlignedBuffer<{{- template "SentSize" .}}>> bytes_;
    {{- else }}
    FIDL_ALIGNDECL
    uint8_t bytes_[FIDL_ALIGN(PrimarySize + MaxOutOfLine)];
    {{- end }}
    UnownedEncodedMessage message_;
  };

  // TODO(fxbug.dev/62485): rename to DecodedMessage.
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
    {{- if .IsResource }}
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

    // These methods should only be used for testing purpose.
    // They create an DecodedMessage using the bytes of an outgoing message and copying the
    // handles.
    static DecodedMessage FromOutgoingWithRawHandleCopy(UnownedEncodedMessage* encoded_message) {
      return DecodedMessage(encoded_message->GetOutgoingMessage());
    }
    static DecodedMessage FromOutgoingWithRawHandleCopy(OwnedEncodedMessage* encoded_message) {
      return DecodedMessage(encoded_message->GetOutgoingMessage());
    }

   private:
    DecodedMessage(::fidl::OutgoingMessage& outgoing_message) {
    {{- if gt .MaxHandles 0 }}
      zx_handle_info_t handles[std::min(ZX_CHANNEL_MAX_MSG_HANDLES, MaxNumHandles)];
      Init(outgoing_message, handles, std::min(ZX_CHANNEL_MAX_MSG_HANDLES, MaxNumHandles));
    {{- else }}
      Init(outgoing_message, nullptr, 0);
    {{- end }}
      if (ok()) {
        Decode<struct {{ .Name }}>();
      }
    }
  };
};
{{- if .IsResource }}
#endif  // __Fuchsia__
{{- end }}
{{- end }}

{{/* TODO(fxbug.dev/36441): Remove __Fuchsia__ ifdefs once we have non-Fuchsia
     emulated handles for C++. */}}
{{- define "StructDefinition" }}
{{ if .IsResource }}
#ifdef __Fuchsia__
{{- end }}
void {{ .Name }}::_CloseHandles() {
  {{- range .Members }}
    {{- template "StructMemberCloseHandles" . }}
  {{- end }}
}
{{- if .IsResource }}
#endif  // __Fuchsia__
{{- end }}
{{- end }}

{{/* TODO(fxbug.dev/36441): Remove __Fuchsia__ ifdefs once we have non-Fuchsia
     emulated handles for C++. */}}
{{- define "StructTraits" }}
{{ if .IsResource }}
#ifdef __Fuchsia__
{{- end }}
template <>
struct IsFidlType<{{ .Namespace }}::{{ .Name }}> : public std::true_type {};
template <>
struct IsStruct<{{ .Namespace }}::{{ .Name }}> : public std::true_type {};
static_assert(std::is_standard_layout_v<{{ .Namespace }}::{{ .Name }}>);
{{- $struct := . }}
{{- range .Members }}
static_assert(offsetof({{ $struct.Namespace }}::{{ $struct.Name }}, {{ .Name }}) == {{ .Offset }});
{{- end }}
static_assert(sizeof({{ .Namespace }}::{{ .Name }}) == {{ .Namespace }}::{{ .Name }}::PrimarySize);
{{- if .IsResource }}
#endif  // __Fuchsia__
{{- end }}
{{- end }}
`
