// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentTableTmpl = `
{{- define "TableForwardDeclaration" }}
class {{ .Name }};
{{- end }}

{{- define "TableMemberCloseHandles" }}
  {{- if .Type.IsResource }}
    if (has_{{ .Name }}()) {
      {{- template "TypeCloseHandles" NewTypedArgument .Name .Type .Type.LLPointer true false }}
    }
  {{- else if .Type.ExternalDeclaration }}
    if constexpr ({{ .Type.LLClass }}::IsResource) {
      if (has_{{ .Name }}()) {
        {{- template "TypeCloseHandles" NewTypedArgument .Name .Type .Type.LLPointer true false }}
      }
    }
  {{- end }}
{{- end }}

{{- define "TableDeclaration" }}

extern "C" const fidl_type_t {{ .TableType }};
{{ range .DocComments }}
//{{ . }}
{{- end}}
class {{ .Name }} final {
public:
  // Returns whether no field is set.
  bool IsEmpty() const { return max_ordinal_ == 0; }

{{- range .Members }}
{{ "" }}
    {{- range .DocComments }}
  //{{ . }}
    {{- end }}
  const {{ .Type.LLDecl }}& {{ .Name }}() const {
    ZX_ASSERT({{ .MethodHasName }}());
    return *frame_ptr_->{{ .Name }}_.data;
  }
  {{ .Type.LLDecl }}& {{ .Name }}() {
    ZX_ASSERT({{ .MethodHasName }}());
    return *frame_ptr_->{{ .Name }}_.data;
  }
  bool {{ .MethodHasName }}() const {
    return max_ordinal_ >= {{ .Ordinal }} && frame_ptr_->{{ .Name }}_.data != nullptr;
  }
  {{- end }}

  {{ .Name }}() = default;
  ~{{ .Name }}() = default;
  {{ .Name }}({{ .Name }}&& other) noexcept = default;
  {{ .Name }}& operator=({{ .Name }}&& other) noexcept = default;

  static constexpr const fidl_type_t* Type = &{{ .TableType }};
  static constexpr uint32_t MaxNumHandles = {{ .MaxHandles }};
  static constexpr uint32_t PrimarySize = {{ .InlineSize }};
  [[maybe_unused]]
  static constexpr uint32_t MaxOutOfLine = {{ .MaxOutOfLine }};
  static constexpr bool HasPointer = {{ .HasPointer }};
  static constexpr bool IsResource = {{ .IsResource }};

  void _CloseHandles();

  // TODO(fxbug.dev/62485): rename to UnownedEncodedMessage.
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
  class OwnedOutgoingMessage final {
   public:
    explicit OwnedOutgoingMessage({{ .Name }}* value)
        {{- if gt .MaxSentSize 512 -}}
      : bytes_(std::make_unique<::fidl::internal::AlignedBuffer<{{- template "SentSize" .}}>>()),
        message_(bytes_->data(), {{- template "SentSize" .}}
        {{- else }}
        : message_(bytes_, sizeof(bytes_)
        {{- end }}
        , value) {}
    OwnedOutgoingMessage(const OwnedOutgoingMessage&) = delete;
    OwnedOutgoingMessage(OwnedOutgoingMessage&&) = delete;
    OwnedOutgoingMessage* operator=(const OwnedOutgoingMessage&) = delete;
    OwnedOutgoingMessage* operator=(OwnedOutgoingMessage&&) = delete;

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
    UnownedOutgoingMessage message_;
  };

  // TODO(fxbug.dev/62485): rename to DecodedMessage.
  class IncomingMessage final : public ::fidl::internal::IncomingMessage {
   public:
    IncomingMessage(const IncomingMessage&) = delete;
    IncomingMessage(IncomingMessage&&) = delete;
    IncomingMessage* operator=(const IncomingMessage&) = delete;
    IncomingMessage* operator=(IncomingMessage&&) = delete;

    {{ .Name }}* PrimaryObject() {
      ZX_DEBUG_ASSERT(ok());
      return reinterpret_cast<{{ .Name }}*>(bytes());
    }

    // These methods should only be used for testing purpose.
    // They create an IncomingMessage using the bytes of an outgoing message and copying the
    // handles.
    static IncomingMessage FromOutgoingWithRawHandleCopy(UnownedOutgoingMessage* outgoing_message) {
      return IncomingMessage(outgoing_message->GetOutgoingMessage());
    }
    static IncomingMessage FromOutgoingWithRawHandleCopy(OwnedOutgoingMessage* outgoing_message) {
      return IncomingMessage(outgoing_message->GetOutgoingMessage());
    }

   private:
    IncomingMessage(::fidl::OutgoingMessage& outgoing_message) {
    {{- if gt .MaxHandles 0 }}
      zx_handle_t handles[std::min(ZX_CHANNEL_MAX_MSG_HANDLES, MaxNumHandles)];
      Init(outgoing_message, handles, std::min(ZX_CHANNEL_MAX_MSG_HANDLES, MaxNumHandles));
    {{- else }}
      Init(outgoing_message, nullptr, 0);
    {{- end }}
      if (ok()) {
        Decode<{{ .Name }}>();
      }
    }
  };

  class Builder;
  class UnownedBuilder;

  class Frame final {
  public:
    Frame() = default;
    // In its intended usage, Frame will be referenced by a tracking_ptr. If the tracking_ptr is
    // assigned before a move or copy, then it will reference the old invalid object. Because this
    // is unsafe, copies are disallowed and moves are only allowed by friend classes that operate
    // safely.
    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;

  private:
    Frame(Frame&&) noexcept = default;
    Frame& operator=(Frame&&) noexcept = default;

    {{- range .FrameItems }}
    ::fidl::Envelope<{{ .LLDecl }}> {{ .Name }}_;
    {{- end }}

    friend class {{ .Name }};
    friend class {{ .Name }}::Builder;
    friend class {{ .Name }}::UnownedBuilder;
  };

 private:
  {{ .Name }}(uint64_t max_ordinal, ::fidl::tracking_ptr<Frame>&& frame_ptr) : max_ordinal_(max_ordinal), frame_ptr_(std::move(frame_ptr)) {}
  uint64_t max_ordinal_ = 0;
  ::fidl::tracking_ptr<Frame> frame_ptr_;
};

// {{ .Name }}::Builder builds {{ .Name }}.
// Usage:
// {{ .Name }} val = {{ .Name }}::Builder(std::make_unique<{{ .Name }}::Frame>())
{{ if ne (len .Members) 0 }}// .set_{{(index .Members 0).Name}}(ptr){{end}}
// .build();
class {{ .Name }}::Builder final {
 public:
  ~Builder() = default;
  Builder() = delete;
  Builder(::fidl::tracking_ptr<{{ .Name }}::Frame>&& frame_ptr) : max_ordinal_(0), frame_ptr_(std::move(frame_ptr)) {}

  Builder(Builder&& other) noexcept = default;
  Builder& operator=(Builder&& other) noexcept = default;

  Builder(const Builder& other) = delete;
  Builder& operator=(const Builder& other) = delete;

  // Returns whether no field is set.
  bool IsEmpty() const { return max_ordinal_ == 0; }

  {{- range .Members }}
{{ "" }}
    {{- range .DocComments }}
  //{{ . }}
    {{- end }}
    {{- /* TODO(fxbug.dev/7999): The elem pointer should be const if it has no handles. */}}
  Builder&& set_{{ .Name }}(::fidl::tracking_ptr<{{ .Type.LLDecl }}> elem) {
    frame_ptr_->{{ .Name }}_.data = std::move(elem);
    if (max_ordinal_ < {{ .Ordinal }}) {
      // Note: the table size is not currently reduced if nullptr is set.
      // This is possible to reconsider in the future.
      max_ordinal_ = {{ .Ordinal }};
    }
    return std::move(*this);
  }
  const {{ .Type.LLDecl }}& {{ .Name }}() const {
    ZX_ASSERT({{ .MethodHasName }}());
    return *frame_ptr_->{{ .Name }}_.data;
  }
  {{ .Type.LLDecl }}& {{ .Name }}() {
    ZX_ASSERT({{ .MethodHasName }}());
    return *frame_ptr_->{{ .Name }}_.data;
  }
  bool {{ .MethodHasName }}() const {
    return max_ordinal_ >= {{ .Ordinal }} && frame_ptr_->{{ .Name }}_.data != nullptr;
  }
  {{- if eq .Type.Kind TableKind }}
  {{ .Type.LLDecl }}::Builder& get_builder_{{ .Name }}() {
    ZX_ASSERT({{ .MethodHasName }}());
    return *reinterpret_cast<{{ .Type.LLDecl }}::Builder*>(&*frame_ptr_->{{ .Name }}_.data);
  }
  {{- end }}
  {{- if eq .Type.Kind ArrayKind }}
  {{- if eq .Type.ElementType.Kind TableKind }}
  ::fidl::Array<{{ .Type.ElementType.LLDecl }}::Builder, {{ .Type.ElementCount }}>& get_builders_{{ .Name }}() {
    ZX_ASSERT({{ .MethodHasName }}());
    return *reinterpret_cast<::fidl::Array<{{ .Type.ElementType.LLDecl }}::Builder, {{ .Type.ElementCount }}>*>(&*frame_ptr_->{{ .Name }}_.data);
  }
  {{- end }}
  {{- end }}
  {{- if eq .Type.Kind VectorKind }}
  {{- if eq .Type.ElementType.Kind TableKind }}
  ::fidl::VectorView<{{ .Type.ElementType.LLDecl }}::Builder>& get_builders_{{ .Name }}() {
    ZX_ASSERT({{ .MethodHasName }}());
    return *reinterpret_cast<::fidl::VectorView<{{ .Type.ElementType.LLDecl }}::Builder>*>(&*frame_ptr_->{{ .Name }}_.data);
  }
  {{- end }}
  {{- end }}
  {{- end }}

  {{ .Name }} build() {
    return {{ .Name }}(max_ordinal_, std::move(frame_ptr_));
  }

private:
  uint64_t max_ordinal_ = 0;
  ::fidl::tracking_ptr<{{ .Name }}::Frame> frame_ptr_;
};

// UnownedBuilder acts like Builder but directly owns its Frame, simplifying working with unowned
// data.
class {{ .Name }}::UnownedBuilder final {
public:
  ~UnownedBuilder() = default;
  UnownedBuilder() noexcept = default;
  UnownedBuilder(UnownedBuilder&& other) noexcept = default;
  UnownedBuilder& operator=(UnownedBuilder&& other) noexcept = default;

  // Returns whether no field is set.
  bool IsEmpty() const { return max_ordinal_ == 0; }

  {{- range .Members }}
{{ "" }}
    {{- range .DocComments }}
  //{{ . }}
    {{- end }}
    {{- /* TODO(fxbug.dev/7999): The elem pointer should be const if it has no handles. */}}
  UnownedBuilder&& set_{{ .Name }}(::fidl::tracking_ptr<{{ .Type.LLDecl }}> elem) {
    ZX_ASSERT(elem);
    frame_.{{ .Name }}_.data = std::move(elem);
    if (max_ordinal_ < {{ .Ordinal }}) {
      max_ordinal_ = {{ .Ordinal }};
    }
    return std::move(*this);
  }
  const {{ .Type.LLDecl }}& {{ .Name }}() const {
    ZX_ASSERT({{ .MethodHasName }}());
    return *frame_.{{ .Name }}_.data;
  }
  {{ .Type.LLDecl }}& {{ .Name }}() {
    ZX_ASSERT({{ .MethodHasName }}());
    return *frame_.{{ .Name }}_.data;
  }
  bool {{ .MethodHasName }}() const {
    return max_ordinal_ >= {{ .Ordinal }} && frame_.{{ .Name }}_.data != nullptr;
  }
  {{- if eq .Type.Kind TableKind }}
  {{ .Type.LLDecl }}::Builder& get_builder_{{ .Name }}() {
    ZX_ASSERT({{ .MethodHasName }}());
    return *reinterpret_cast<{{ .Type.LLDecl }}::Builder*>(&*frame_.{{ .Name }}_.data);
  }
  {{- end }}
  {{- if eq .Type.Kind ArrayKind }}
  {{- if eq .Type.ElementType.Kind TableKind }}
  ::fidl::Array<{{ .Type.ElementType.LLDecl }}::Builder, {{ .Type.ElementCount }}>& get_builders_{{ .Name }}() {
    ZX_ASSERT({{ .MethodHasName }}());
    return *reinterpret_cast<::fidl::Array<{{ .Type.ElementType.LLDecl }}::Builder, {{ .Type.ElementCount }}>*>(&*frame_.{{ .Name }}_.data);
  }
  {{- end }}
  {{- end }}
  {{- if eq .Type.Kind VectorKind }}
  {{- if eq .Type.ElementType.Kind TableKind }}
  ::fidl::VectorView<{{ .Type.ElementType.LLDecl }}::Builder>& get_builders_{{ .Name }}() {
    ZX_ASSERT({{ .MethodHasName }}());
    return *reinterpret_cast<::fidl::VectorView<{{ .Type.ElementType.LLDecl }}::Builder>*>(&*frame_.{{ .Name }}_.data);
  }
  {{- end }}
  {{- end }}
  {{- end }}

  {{ .Name }} build() {
    {{ if eq (len .Members) 0 -}}
    return {{ .Name }}(max_ordinal_, nullptr);
    {{- else -}}
    return {{ .Name }}(max_ordinal_, ::fidl::unowned_ptr(&frame_));
    {{- end }}
  }

private:
  uint64_t max_ordinal_ = 0;
  {{ if ne (len .Members) 0 -}}
  {{ .Name }}::Frame frame_;
  {{- end }}
};

{{- end }}

{{- define "TableDefinition" }}

void {{ .Name }}::_CloseHandles() {
  {{- range .Members }}
    {{- template "TableMemberCloseHandles" . }}
  {{- end }}
}
{{- end }}

{{- define "TableTraits" }}

template <>
struct IsFidlType<{{ .Namespace }}::{{ .Name }}> : public std::true_type {};
template <>
struct IsTable<{{ .Namespace }}::{{ .Name }}> : public std::true_type {};
template <>
struct IsTableBuilder<{{ .Namespace }}::{{ .Name }}::Builder> : public std::true_type {};
static_assert(std::is_standard_layout_v<{{ .Namespace }}::{{ .Name }}>);
{{- end }}
`
