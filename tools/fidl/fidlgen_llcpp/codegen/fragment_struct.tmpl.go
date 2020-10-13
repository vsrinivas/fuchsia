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

{{- define "StructSentSize"}}
  {{- if gt .MaxSentSize 65536 -}}
  ZX_CHANNEL_MAX_MSG_BYTES
  {{- else -}}
  PrimarySize + MaxOutOfLine
  {{- end -}}
{{- end }}

{{- define "StructDeclaration" }}

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

  class UnownedOutgoingMessage final {
   public:
    UnownedOutgoingMessage(uint8_t* bytes, uint32_t byte_size, {{ .Name }}* value)
        : message_(bytes, byte_size, sizeof({{ .Name }}),
    {{- if gt .MaxHandles 0 }}
      handles_, std::min(ZX_CHANNEL_MAX_MSG_HANDLES, MaxNumHandles), 0
    {{- else }}
      nullptr, 0, 0
    {{- end }}
      ) {
      message_.LinearizeAndEncode<{{ .Name }}>(value);
    }
    UnownedOutgoingMessage(const UnownedOutgoingMessage&) = delete;
    UnownedOutgoingMessage(UnownedOutgoingMessage&&) = delete;
    UnownedOutgoingMessage* operator=(const UnownedOutgoingMessage&) = delete;
    UnownedOutgoingMessage* operator=(UnownedOutgoingMessage&&) = delete;

    zx_status_t status() const { return message_.status(); }
    bool ok() const { return message_.status() == ZX_OK; }
    const char* error() const { return message_.error(); }

    ::fidl::OutgoingMessage& GetOutgoingMessage() { return message_; }

   private:
    {{- if gt .MaxHandles 0 }}
      zx_handle_t handles_[std::min(ZX_CHANNEL_MAX_MSG_HANDLES, MaxNumHandles)];
    {{- end }}
    ::fidl::OutgoingMessage message_;
  };

  class OwnedOutgoingMessage final {
   public:
    explicit OwnedOutgoingMessage({{ .Name }}* value)
        {{- if gt .MaxSentSize 512 -}}
      : bytes_(std::make_unique<::fidl::internal::AlignedBuffer<{{- template "StructSentSize" .}}>>()),
        message_(bytes_->data(), {{- template "StructSentSize" .}}
        {{- else }}
        : message_(bytes_, sizeof(bytes_)
        {{- end }}
        , value) {}
    OwnedOutgoingMessage(const OwnedOutgoingMessage&) = delete;
    OwnedOutgoingMessage(OwnedOutgoingMessage&&) = delete;
    OwnedOutgoingMessage* operator=(const OwnedOutgoingMessage&) = delete;
    OwnedOutgoingMessage* operator=(OwnedOutgoingMessage&&) = delete;

    zx_status_t status() const { return message_.status(); }
    bool ok() const { return message_.ok(); }
    const char* error() const { return message_.error(); }

    ::fidl::OutgoingMessage& GetOutgoingMessage() { return message_.GetOutgoingMessage(); }

   private:
    {{- if gt .MaxSentSize 512 }}
    std::unique_ptr<::fidl::internal::AlignedBuffer<{{- template "StructSentSize" .}}>> bytes_;
    {{- else }}
    FIDL_ALIGNDECL
    uint8_t bytes_[PrimarySize + MaxOutOfLine];
    {{- end }}
    UnownedOutgoingMessage message_;
  };
};
{{- end }}

{{- define "StructDefinition" }}

void {{ .Name }}::_CloseHandles() {
  {{- range .Members }}
    {{- template "StructMemberCloseHandles" . }}
  {{- end }}
}
{{- end }}

{{- define "StructTraits" }}

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
{{- end }}
`
