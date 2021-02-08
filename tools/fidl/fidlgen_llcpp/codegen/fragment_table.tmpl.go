// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentTableTmpl = `
{{- define "TableForwardDeclaration" }}
{{ EnsureNamespace .Decl.Wire }}
class {{ .Decl.Wire.Name }};
{{- end }}

{{- define "TableMemberCloseHandles" }}
  {{- if .Type.IsResource }}
    if (has_{{ .Name }}()) {
      {{- template "TypeCloseHandles" NewTypedArgument .Name .Type .Type.WirePointer true false }}
    }
  {{- end }}
{{- end }}

{{/* TODO(fxbug.dev/36441): Remove __Fuchsia__ ifdefs once we have non-Fuchsia
     emulated handles for C++. */}}
{{- define "TableDeclaration" }}
{{ EnsureNamespace .Decl.Wire }}
{{ if .IsResourceType }}
#ifdef __Fuchsia__
{{- PushNamespace }}
{{- end }}

extern "C" const fidl_type_t {{ .TableType }};
{{ range .DocComments }}
//{{ . }}
{{- end}}
class {{ .Decl.Wire.Name }} final {
public:
  // Returns whether no field is set.
  bool IsEmpty() const { return max_ordinal_ == 0; }

  class Frame;

{{- range .Members }}
{{ "" }}
    {{- range .DocComments }}
  //{{ . }}
    {{- end }}
  const {{ .Type.Wire }}& {{ .Name }}() const {
    ZX_ASSERT({{ .MethodHasName }}());
    return *frame_ptr_->{{ .Name }}_.data;
  }
  {{ .Type.Wire }}& {{ .Name }}() {
    ZX_ASSERT({{ .MethodHasName }}());
    return *frame_ptr_->{{ .Name }}_.data;
  }
  bool {{ .MethodHasName }}() const {
    return max_ordinal_ >= {{ .Ordinal }} && frame_ptr_->{{ .Name }}_.data != nullptr;
  }
  {{- /* TODO(fxbug.dev/7999): The elem pointer should be const if it has no handles. */}}
  void set_{{ .Name }}(::fidl::ObjectView<{{ .Type.Wire }}> elem) {
    ZX_DEBUG_ASSERT(frame_ptr_.get() != nullptr);
    frame_ptr_->{{ .Name }}_.data = elem;
    max_ordinal_ = std::max(max_ordinal_, static_cast<uint64_t>({{ .Ordinal }}));
  }
  void set_{{ .Name }}(std::nullptr_t) {
    ZX_DEBUG_ASSERT(frame_ptr_.get() != nullptr);
    frame_ptr_->{{ .Name }}_.data = nullptr;
  }
  template <typename... Args>
  void set_{{ .Name }}(::fidl::AnyAllocator& allocator, Args&&... args) {
    ZX_DEBUG_ASSERT(frame_ptr_.get() != nullptr);
    frame_ptr_->{{ .Name }}_.data =
        ::fidl::ObjectView<{{ .Type.Wire }}>(allocator, std::forward<Args>(args)...);
    max_ordinal_ = std::max(max_ordinal_, static_cast<uint64_t>({{ .Ordinal }}));
  }
  template <typename... Args>
  void set_{{ .Name }}(::fidl::Allocator& allocator, Args&&... args) {
    ZX_DEBUG_ASSERT(frame_ptr_.get() != nullptr);
    frame_ptr_->{{ .Name }}_.data =
        ::fidl::tracking_ptr<{{ .Type.Wire }}>(allocator, std::forward<Args>(args)...);
    max_ordinal_ = std::max(max_ordinal_, static_cast<uint64_t>({{ .Ordinal }}));
  }
  void set_{{ .Name }}(::fidl::tracking_ptr<{{ .Type.Wire }}> elem) {
    frame_ptr_->{{ .Name }}_.data = std::move(elem);
    max_ordinal_ = std::max(max_ordinal_, static_cast<uint64_t>({{ .Ordinal }}));
  }
  {{- end }}

  {{ .Decl.Wire.Name }}() = default;
  explicit {{ .Decl.Wire.Name }}(::fidl::AnyAllocator& allocator)
      : frame_ptr_(::fidl::ObjectView<Frame>(allocator)) {}
  explicit {{ .Decl.Wire.Name }}(::fidl::Allocator& allocator)
      : frame_ptr_(::fidl::tracking_ptr<Frame>(allocator)) {}
  explicit {{ .Decl.Wire.Name }}(::fidl::tracking_ptr<Frame>&& frame_ptr)
      : frame_ptr_(std::move(frame_ptr)) {}
  ~{{ .Decl.Wire.Name }}() = default;
  {{ .Decl.Wire.Name }}({{ .Decl.Wire.Name }}&& other) noexcept = default;
  {{ .Decl.Wire.Name }}& operator=({{ .Decl.Wire.Name }}&& other) noexcept = default;

  static constexpr const fidl_type_t* Type = &{{ .TableType }};
  static constexpr uint32_t MaxNumHandles = {{ .MaxHandles }};
  static constexpr uint32_t PrimarySize = {{ .InlineSize }};
  [[maybe_unused]]
  static constexpr uint32_t MaxOutOfLine = {{ .MaxOutOfLine }};
  static constexpr bool HasPointer = {{ .HasPointer }};

  void Allocate(::fidl::AnyAllocator& allocator) {
    max_ordinal_ = 0;
    frame_ptr_ = ::fidl::ObjectView<Frame>(allocator);
  }
  void Allocate(::fidl::Allocator& allocator) {
    max_ordinal_ = 0;
    frame_ptr_ = ::fidl::tracking_ptr<Frame>(allocator);
  }
  void Init(::fidl::tracking_ptr<Frame>&& frame_ptr) {
    max_ordinal_ = 0;
    frame_ptr_ = std::move(frame_ptr);
  }

  {{- if .IsResourceType }}

  void _CloseHandles();
  {{- end }}

  class UnownedEncodedMessage final {
   public:
    UnownedEncodedMessage(uint8_t* bytes, uint32_t byte_size, {{ .Decl.Wire.Name }}* value)
        : message_(bytes, byte_size, sizeof({{ .Decl.Wire.Name }}),
    {{- if gt .MaxHandles 0 }}
      handles_, std::min(ZX_CHANNEL_MAX_MSG_HANDLES, MaxNumHandles), 0
    {{- else }}
      nullptr, 0, 0
    {{- end }}
      ) {
      message_.Encode<{{ .Decl.Wire.Name }}>(value);
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

    ::fidl::OutgoingByteMessage& GetOutgoingMessage() { return message_; }

   private:
    {{- if gt .MaxHandles 0 }}
      zx_handle_disposition_t handles_[std::min(ZX_CHANNEL_MAX_MSG_HANDLES, MaxNumHandles)];
    {{- end }}
    ::fidl::OutgoingByteMessage message_;
  };

  class OwnedEncodedMessage final {
   public:
    explicit OwnedEncodedMessage({{ .Decl.Wire.Name }}* value)
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

    ::fidl::OutgoingByteMessage& GetOutgoingMessage() { return message_.GetOutgoingMessage(); }

   private:
    {{- if gt .MaxSentSize 512 }}
    std::unique_ptr<::fidl::internal::AlignedBuffer<{{- template "SentSize" .}}>> bytes_;
    {{- else }}
    FIDL_ALIGNDECL
    uint8_t bytes_[FIDL_ALIGN(PrimarySize + MaxOutOfLine)];
    {{- end }}
    UnownedEncodedMessage message_;
  };

  class DecodedMessage final : public ::fidl::internal::IncomingMessage {
   public:
    DecodedMessage(uint8_t* bytes, uint32_t byte_actual, zx_handle_info_t* handles = nullptr,
                    uint32_t handle_actual = 0)
        : ::fidl::internal::IncomingMessage(bytes, byte_actual, handles, handle_actual) {
      Decode<{{ .Decl.Wire.Name }}>();
    }
    DecodedMessage(fidl_incoming_msg_t* msg) : ::fidl::internal::IncomingMessage(msg) {
      Decode<{{ .Decl.Wire.Name }}>();
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

    {{ .Decl.Wire.Name }}* PrimaryObject() {
      ZX_DEBUG_ASSERT(ok());
      return reinterpret_cast<{{ .Decl.Wire.Name }}*>(bytes());
    }

    // Release the ownership of the decoded message. That means that the handles won't be closed
    // When the object is destroyed.
    // After calling this method, the DecodedMessage object should not be used anymore.
    void ReleasePrimaryObject() { ResetBytes(); }
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

    {{- range $index, $item := .FrameItems }}
      {{- if $item }}
    ::fidl::Envelope<{{ $item.Type.Wire }}> {{ $item.Name }}_;
      {{- else }}
    ::fidl::Envelope<void> reserved_{{ $index }}_;
      {{- end }}
    {{- end }}

    friend class {{ .Decl.Wire.Name }};
    friend class {{ .Decl.Wire.Name }}::Builder;
    friend class {{ .Decl.Wire.Name }}::UnownedBuilder;
  };

 private:
  {{ .Decl.Wire.Name }}(uint64_t max_ordinal, ::fidl::tracking_ptr<Frame>&& frame_ptr) : max_ordinal_(max_ordinal), frame_ptr_(std::move(frame_ptr)) {}
  uint64_t max_ordinal_ = 0;
  ::fidl::tracking_ptr<Frame> frame_ptr_;
};

// {{ .Decl.Wire.Name }}::Builder builds {{ .Decl.Wire.Name }}.
// Usage:
// {{ .Decl.Wire.Name }} val = {{ .Decl.Wire.Name }}::Builder(std::make_unique<{{ .Decl.Wire.Name }}::Frame>())
{{ if ne (len .Members) 0 }}// .set_{{(index .Members 0).Name}}(ptr){{end}}
// .build();
class {{ .Decl.Wire.Name }}::Builder final {
 public:
  ~Builder() = default;
  Builder() = delete;
  Builder(::fidl::tracking_ptr<{{ .Decl.Wire.Name }}::Frame>&& frame_ptr) : max_ordinal_(0), frame_ptr_(std::move(frame_ptr)) {}

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
  Builder&& set_{{ .Name }}(::fidl::tracking_ptr<{{ .Type.Wire }}> elem) {
    frame_ptr_->{{ .Name }}_.data = std::move(elem);
    if (max_ordinal_ < {{ .Ordinal }}) {
      // Note: the table size is not currently reduced if nullptr is set.
      // This is possible to reconsider in the future.
      max_ordinal_ = {{ .Ordinal }};
    }
    return std::move(*this);
  }
  const {{ .Type.Wire }}& {{ .Name }}() const {
    ZX_ASSERT({{ .MethodHasName }}());
    return *frame_ptr_->{{ .Name }}_.data;
  }
  {{ .Type.Wire }}& {{ .Name }}() {
    ZX_ASSERT({{ .MethodHasName }}());
    return *frame_ptr_->{{ .Name }}_.data;
  }
  bool {{ .MethodHasName }}() const {
    return max_ordinal_ >= {{ .Ordinal }} && frame_ptr_->{{ .Name }}_.data != nullptr;
  }
  {{- if eq .Type.Kind TypeKinds.Table }}
  {{ .Type.Wire }}::Builder& get_builder_{{ .Name }}() {
    ZX_ASSERT({{ .MethodHasName }}());
    return *reinterpret_cast<{{ .Type.Wire }}::Builder*>(&*frame_ptr_->{{ .Name }}_.data);
  }
  {{- end }}
  {{- if eq .Type.Kind TypeKinds.Array }}
  {{- if eq .Type.ElementType.Kind TypeKinds.Table }}
  ::fidl::Array<{{ .Type.ElementType.Wire }}::Builder, {{ .Type.ElementCount }}>& get_builders_{{ .Name }}() {
    ZX_ASSERT({{ .MethodHasName }}());
    return *reinterpret_cast<::fidl::Array<{{ .Type.ElementType.Wire }}::Builder, {{ .Type.ElementCount }}>*>(&*frame_ptr_->{{ .Name }}_.data);
  }
  {{- end }}
  {{- end }}
  {{- if eq .Type.Kind TypeKinds.Vector }}
  {{- if eq .Type.ElementType.Kind TypeKinds.Table }}
  ::fidl::VectorView<{{ .Type.ElementType.Wire }}::Builder>& get_builders_{{ .Name }}() {
    ZX_ASSERT({{ .MethodHasName }}());
    return *reinterpret_cast<::fidl::VectorView<{{ .Type.ElementType.Wire }}::Builder>*>(&*frame_ptr_->{{ .Name }}_.data);
  }
  {{- end }}
  {{- end }}
  {{- end }}

  {{ .Decl.Wire.Name }} build() {
    return {{ .Decl.Wire.Name }}(max_ordinal_, std::move(frame_ptr_));
  }

private:
  uint64_t max_ordinal_ = 0;
  ::fidl::tracking_ptr<{{ .Decl.Wire.Name }}::Frame> frame_ptr_;
};

// UnownedBuilder acts like Builder but directly owns its Frame, simplifying working with unowned
// data.
class {{ .Decl.Wire.Name }}::UnownedBuilder final {
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
  UnownedBuilder&& set_{{ .Name }}(::fidl::tracking_ptr<{{ .Type.Wire }}> elem) {
    ZX_ASSERT(elem);
    frame_.{{ .Name }}_.data = std::move(elem);
    if (max_ordinal_ < {{ .Ordinal }}) {
      max_ordinal_ = {{ .Ordinal }};
    }
    return std::move(*this);
  }
  const {{ .Type.Wire }}& {{ .Name }}() const {
    ZX_ASSERT({{ .MethodHasName }}());
    return *frame_.{{ .Name }}_.data;
  }
  {{ .Type.Wire }}& {{ .Name }}() {
    ZX_ASSERT({{ .MethodHasName }}());
    return *frame_.{{ .Name }}_.data;
  }
  bool {{ .MethodHasName }}() const {
    return max_ordinal_ >= {{ .Ordinal }} && frame_.{{ .Name }}_.data != nullptr;
  }
  {{- if eq .Type.Kind TypeKinds.Table }}
  {{ .Type.Wire }}::Builder& get_builder_{{ .Name }}() {
    ZX_ASSERT({{ .MethodHasName }}());
    return *reinterpret_cast<{{ .Type.Wire }}::Builder*>(&*frame_.{{ .Name }}_.data);
  }
  {{- end }}
  {{- if eq .Type.Kind TypeKinds.Array }}
  {{- if eq .Type.ElementType.Kind TypeKinds.Table }}
  ::fidl::Array<{{ .Type.ElementType.Wire }}::Builder, {{ .Type.ElementCount }}>& get_builders_{{ .Name }}() {
    ZX_ASSERT({{ .MethodHasName }}());
    return *reinterpret_cast<::fidl::Array<{{ .Type.ElementType.Wire }}::Builder, {{ .Type.ElementCount }}>*>(&*frame_.{{ .Name }}_.data);
  }
  {{- end }}
  {{- end }}
  {{- if eq .Type.Kind TypeKinds.Vector }}
  {{- if eq .Type.ElementType.Kind TypeKinds.Table }}
  ::fidl::VectorView<{{ .Type.ElementType.Wire }}::Builder>& get_builders_{{ .Name }}() {
    ZX_ASSERT({{ .MethodHasName }}());
    return *reinterpret_cast<::fidl::VectorView<{{ .Type.ElementType.Wire }}::Builder>*>(&*frame_.{{ .Name }}_.data);
  }
  {{- end }}
  {{- end }}
  {{- end }}

  {{ .Decl.Wire.Name }} build() {
    {{ if eq (len .Members) 0 -}}
    return {{ .Decl.Wire.Name }}(max_ordinal_, nullptr);
    {{- else -}}
    return {{ .Decl.Wire.Name }}(max_ordinal_, ::fidl::unowned_ptr(&frame_));
    {{- end }}
  }

private:
  uint64_t max_ordinal_ = 0;
  {{ if ne (len .Members) 0 -}}
  {{ .Decl.Wire.Name }}::Frame frame_;
  {{- end }}
};

{{- EnsureNamespace .WireAlias }}
using {{ .WireAlias.Name }} = {{ .Decl.Wire }};

{{- if .IsResourceType }}
{{- PopNamespace }}
#endif  // __Fuchsia__
{{ end }}
{{- end }}

{{/* TODO(fxbug.dev/36441): Remove __Fuchsia__ ifdefs once we have non-Fuchsia
     emulated handles for C++. */}}
{{- define "TableDefinition" }}
{{ if .IsResourceType }}
{{ EnsureNamespace "::" }}
#ifdef __Fuchsia__
{{- PushNamespace }}
void {{ .Decl.Wire }}::_CloseHandles() {
  {{- range .Members }}
    {{- template "TableMemberCloseHandles" . }}
  {{- end }}
}
{{- PopNamespace }}
#endif  // __Fuchsia__
{{- end }}
{{- end }}

{{/* TODO(fxbug.dev/36441): Remove __Fuchsia__ ifdefs once we have non-Fuchsia
     emulated handles for C++. */}}
{{- define "TableTraits" }}
{{ if .IsResourceType }}
#ifdef __Fuchsia__
{{- PushNamespace }}
{{- end }}
template <>
struct IsFidlType<{{ .Decl.Wire }}> : public std::true_type {};
template <>
struct IsTable<{{ .Decl.Wire }}> : public std::true_type {};
template <>
struct IsTableBuilder<{{ .Decl.Wire }}::Builder> : public std::true_type {};
static_assert(std::is_standard_layout_v<{{ .Decl.Wire }}>);
{{- if .IsResourceType }}
{{- PopNamespace }}
#endif  // __Fuchsia__
{{- end }}
{{- end }}
`
