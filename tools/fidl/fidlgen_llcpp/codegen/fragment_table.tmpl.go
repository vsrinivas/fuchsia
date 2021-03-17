// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentTableTmpl = `
{{- define "TableForwardDeclaration" }}
{{ EnsureNamespace . }}
class {{ .Name }};
{{- end }}

{{/* TODO(fxbug.dev/36441): Remove __Fuchsia__ ifdefs once we have non-Fuchsia
     emulated handles for C++. */}}
{{- define "TableDeclaration" }}
{{ EnsureNamespace . }}
{{ if .IsResourceType }}
#ifdef __Fuchsia__
{{- PushNamespace }}
{{- end }}

extern "C" const fidl_type_t {{ .CodingTableType }};
{{ range .DocComments }}
//{{ . }}
{{- end }}
class {{ .Name }} final {
public:
  // Returns whether no field is set.
  bool IsEmpty() const { return max_ordinal_ == 0; }

  class Frame;

{{- range .Members }}
{{ "" }}
    {{- range .DocComments }}
  //{{ . }}
    {{- end }}
  const {{ .Type }}& {{ .Name }}() const {
    ZX_ASSERT({{ .MethodHasName }}());
    return *frame_ptr_->{{ .Name }}_.data;
  }
  {{ .Type }}& {{ .Name }}() {
    ZX_ASSERT({{ .MethodHasName }}());
    return *frame_ptr_->{{ .Name }}_.data;
  }
  bool {{ .MethodHasName }}() const {
    return max_ordinal_ >= {{ .Ordinal }} && frame_ptr_->{{ .Name }}_.data != nullptr;
  }
  {{- /* TODO(fxbug.dev/7999): The elem pointer should be const if it has no handles. */}}
  {{ $.Name }}& set_{{ .Name }}(::fidl::ObjectView<{{ .Type }}> elem) {
    ZX_DEBUG_ASSERT(frame_ptr_.get() != nullptr);
    frame_ptr_->{{ .Name }}_.data = elem;
    max_ordinal_ = std::max(max_ordinal_, static_cast<uint64_t>({{ .Ordinal }}));
    return *this;
  }
  {{ $.Name }}& set_{{ .Name }}(std::nullptr_t) {
    ZX_DEBUG_ASSERT(frame_ptr_.get() != nullptr);
    frame_ptr_->{{ .Name }}_.data = nullptr;
    return *this;
  }
  template <typename... Args>
  {{ $.Name }}& set_{{ .Name }}(::fidl::AnyAllocator& allocator, Args&&... args) {
    ZX_DEBUG_ASSERT(frame_ptr_.get() != nullptr);
    frame_ptr_->{{ .Name }}_.data =
        ::fidl::ObjectView<{{ .Type }}>(allocator, std::forward<Args>(args)...);
    max_ordinal_ = std::max(max_ordinal_, static_cast<uint64_t>({{ .Ordinal }}));
    return *this;
  }
  {{ $.Name }}& set_{{ .Name }}(::fidl::tracking_ptr<{{ .Type }}> elem) {
    frame_ptr_->{{ .Name }}_.data = std::move(elem);
    max_ordinal_ = std::max(max_ordinal_, static_cast<uint64_t>({{ .Ordinal }}));
    return *this;
  }
  {{- end }}

  {{ .Name }}() = default;
  explicit {{ .Name }}(::fidl::AnyAllocator& allocator)
      : frame_ptr_(::fidl::ObjectView<Frame>(allocator)) {}
  // This constructor allows a user controlled allocation (not using a FidlAllocator).
  // It should only be used when performance is key.
  // As soon as the frame is given to the table, it must not be used directly or for another table.
  explicit {{ .Name }}(::fidl::ObjectView<Frame>&& frame)
      : frame_ptr_(std::move(frame)) {}
  explicit {{ .Name }}(::fidl::tracking_ptr<Frame>&& frame_ptr)
      : frame_ptr_(std::move(frame_ptr)) {}
  ~{{ .Name }}() = default;
  {{ .Name }}({{ .Name }}&& other) noexcept = default;
  {{ .Name }}& operator=({{ .Name }}&& other) noexcept = default;

  static constexpr const fidl_type_t* Type = &{{ .CodingTableType }};
  static constexpr uint32_t MaxNumHandles = {{ .MaxHandles }};
  static constexpr uint32_t PrimarySize = {{ .InlineSize }};
  [[maybe_unused]]
  static constexpr uint32_t MaxOutOfLine = {{ .MaxOutOfLine }};
  static constexpr bool HasPointer = {{ .HasPointer }};

  void Allocate(::fidl::AnyAllocator& allocator) {
    max_ordinal_ = 0;
    frame_ptr_ = ::fidl::ObjectView<Frame>(allocator);
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
    UnownedEncodedMessage(uint8_t* bytes, uint32_t byte_size, {{ .Name }}* value)
        : message_(bytes, byte_size, sizeof({{ .Name }}),
    {{- if gt .MaxHandles 0 }}
      handles_, std::min(ZX_CHANNEL_MAX_MSG_HANDLES, MaxNumHandles), 0
    {{- else }}
      nullptr, 0, 0
    {{- end }}
      ) {
      message_.Encode<{{ .Name }}>(value);
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
      zx_handle_disposition_t handles_[std::min(ZX_CHANNEL_MAX_MSG_HANDLES, MaxNumHandles)];
    {{- end }}
    ::fidl::OutgoingMessage message_;
  };

  class OwnedEncodedMessage final {
   public:
    explicit OwnedEncodedMessage({{ .Name }}* value)
      : message_(bytes_.data(), bytes_.size(), value) {}
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
    {{ .ByteBufferType }} bytes_;
    UnownedEncodedMessage message_;
  };

  class DecodedMessage final : public ::fidl::internal::IncomingMessage {
   public:
    DecodedMessage(uint8_t* bytes, uint32_t byte_actual, zx_handle_info_t* handles = nullptr,
                    uint32_t handle_actual = 0)
        : ::fidl::internal::IncomingMessage(bytes, byte_actual, handles, handle_actual) {
      Decode<{{ .Name }}>();
    }
    DecodedMessage(fidl_incoming_msg_t* msg) : ::fidl::internal::IncomingMessage(msg) {
      Decode<{{ .Name }}>();
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

    {{ .Name }}* PrimaryObject() {
      ZX_DEBUG_ASSERT(ok());
      return reinterpret_cast<{{ .Name }}*>(bytes());
    }

    // Release the ownership of the decoded message. That means that the handles won't be closed
    // When the object is destroyed.
    // After calling this method, the DecodedMessage object should not be used anymore.
    void ReleasePrimaryObject() { ResetBytes(); }
  };

  // Frames are managed automatically by the FidlAllocator class.
  // The only direct usage is when performance is key and a frame needs to be allocated outside a
  // FidlAllocator.
  // Once created, a frame can only be used for one single table.
  class Frame final {
  public:
    Frame() = default;
    // In its intended usage, Frame will be referenced by an ObjectView. If the ObjectView is
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
    ::fidl::Envelope<{{ $item.Type }}> {{ $item.Name }}_;
      {{- else }}
    ::fidl::Envelope<void> reserved_{{ $index }}_;
      {{- end }}
    {{- end }}

    friend class {{ .Name }};
  };

 private:
  {{ .Name }}(uint64_t max_ordinal, ::fidl::tracking_ptr<Frame>&& frame_ptr) : max_ordinal_(max_ordinal), frame_ptr_(std::move(frame_ptr)) {}
  uint64_t max_ordinal_ = 0;
  ::fidl::tracking_ptr<Frame> frame_ptr_;
};

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
void {{ . }}::_CloseHandles() {
  {{- range .Members }}
    {{- if .Type.IsResource }}
      if (has_{{ .Name }}()) {
        {{- CloseHandles . true false }}
      }
    {{- end }}
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
struct IsFidlType<{{ . }}> : public std::true_type {};
template <>
struct IsTable<{{ . }}> : public std::true_type {};
static_assert(std::is_standard_layout_v<{{ . }}>);
{{- if .IsResourceType }}
{{- PopNamespace }}
#endif  // __Fuchsia__
{{- end }}
{{- end }}
`
