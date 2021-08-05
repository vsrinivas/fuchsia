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
{{- IfdefFuchsia -}}
{{- end }}

extern "C" const fidl_type_t {{ .CodingTableType }};
{{ .Docs }}
class {{ .Name }} final {
public:
  // Returns whether no field is set.
  bool IsEmpty() const { return max_ordinal_ == 0; }

  class Frame_;

{{- range .Members }}
{{ "" }}
  {{- .Docs }}
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
    ZX_DEBUG_ASSERT(frame_ptr_ != nullptr);
    frame_ptr_->{{ .Name }}_.data = elem;
    max_ordinal_ = std::max(max_ordinal_, static_cast<uint64_t>({{ .Ordinal }}));
    return *this;
  }
  {{ $.Name }}& set_{{ .Name }}(std::nullptr_t) {
    ZX_DEBUG_ASSERT(frame_ptr_ != nullptr);
    frame_ptr_->{{ .Name }}_.data = nullptr;
    return *this;
  }
  template <typename... Args>
  {{ $.Name }}& set_{{ .Name }}(::fidl::AnyArena& allocator, Args&&... args) {
    ZX_DEBUG_ASSERT(frame_ptr_ != nullptr);
    frame_ptr_->{{ .Name }}_.data =
        ::fidl::ObjectView<{{ .Type }}>(allocator, std::forward<Args>(args)...);
    max_ordinal_ = std::max(max_ordinal_, static_cast<uint64_t>({{ .Ordinal }}));
    return *this;
  }
  {{- end }}

  {{ .Name }}() = default;
  explicit {{ .Name }}(::fidl::AnyArena& allocator)
      : frame_ptr_(::fidl::ObjectView<Frame_>(allocator)) {}
  // This constructor allows a user controlled allocation (not using a Arena).
  // It should only be used when performance is key.
  // As soon as the frame is given to the table, it must not be used directly or for another table.
  explicit {{ .Name }}(::fidl::ObjectView<Frame_>&& frame)
      : frame_ptr_(std::move(frame)) {}
  ~{{ .Name }}() = default;
  {{ .Name }}(const {{ .Name }}& other) noexcept = default;
  {{ .Name }}& operator=(const {{ .Name }}& other) noexcept = default;
  {{ .Name }}({{ .Name }}&& other) noexcept = default;
  {{ .Name }}& operator=({{ .Name }}&& other) noexcept = default;

  static constexpr const fidl_type_t* Type = &{{ .CodingTableType }};
  static constexpr uint32_t MaxNumHandles = {{ .TypeShapeV1.MaxHandles }};
  static constexpr uint32_t PrimarySize = {{ .TypeShapeV1.InlineSize }};
  [[maybe_unused]]
  static constexpr uint32_t MaxOutOfLine = {{ .TypeShapeV1.MaxOutOfLine }};
  static constexpr bool HasPointer = {{ .TypeShapeV1.HasPointer }};

  void Allocate(::fidl::AnyArena& allocator) {
    max_ordinal_ = 0;
    frame_ptr_ = ::fidl::ObjectView<Frame_>(allocator);
  }
  void Init(::fidl::ObjectView<Frame_>&& frame_ptr) {
    max_ordinal_ = 0;
    frame_ptr_ = std::move(frame_ptr);
  }

  {{- if .IsResourceType }}

  void _CloseHandles();
  {{- end }}

  class UnownedEncodedMessage final {
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

  class OwnedEncodedMessage final {
   public:
    explicit OwnedEncodedMessage({{ .Name }}* value)
      : message_(1u, backing_buffer_.data(), backing_buffer_.size(), value) {}
    // Construct a message using owned buffers.
    // If |iovec_capacity>1|, then the message in OwnedEncodedMessage may point the input FIDL
    // object, which is not owned by it.
    explicit OwnedEncodedMessage(::fidl::internal::AllowUnownedInputRef allow_unowned, {{ .Name }}* value)
      : message_(::fidl::internal::IovecBufferSize, backing_buffer_.data(), backing_buffer_.size(), value) {}
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

  class DecodedMessage final : public ::fidl::internal::DecodedMessageBase<{{ .Name }}> {
   public:
    using DecodedMessageBase<{{ .Name }}>::DecodedMessageBase;

    DecodedMessage(uint8_t* bytes, uint32_t byte_actual, zx_handle_info_t* handles = nullptr,
                   uint32_t handle_actual = 0)
        : DecodedMessageBase(
              ::fidl::IncomingMessage(bytes, byte_actual, handles, handle_actual,
                  ::fidl::IncomingMessage::kSkipMessageHeaderValidation)) {}

    DecodedMessage(const fidl_incoming_msg_t* c_msg)
        : DecodedMessage(reinterpret_cast<uint8_t*>(c_msg->bytes), c_msg->num_bytes,
                         c_msg->handles, c_msg->num_handles) {}

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

  // Frame_s are managed automatically by the Arena class.
  // The only direct usage is when performance is key and a frame needs to be allocated outside a
  // Arena.
  // Once created, a frame can only be used for one single table.
  class Frame_ final {
  public:
    Frame_() = default;
    // In its intended usage, Frame_ will be referenced by an ObjectView. If the ObjectView is
    // assigned before a move or copy, then it will reference the old invalid object. Because this
    // is unsafe, copies are disallowed and moves are only allowed by friend classes that operate
    // safely.
    Frame_(const Frame_&) = delete;
    Frame_& operator=(const Frame_&) = delete;

  private:
    Frame_(Frame_&&) noexcept = default;
    Frame_& operator=(Frame_&&) noexcept = default;

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
  uint64_t max_ordinal_ = 0;
  ::fidl::ObjectView<Frame_> frame_ptr_;
};

{{- if .IsResourceType }}
{{- EndifFuchsia -}}
{{ end }}
{{- end }}

{{/* TODO(fxbug.dev/36441): Remove __Fuchsia__ ifdefs once we have non-Fuchsia
     emulated handles for C++. */}}
{{- define "TableDefinition" }}
{{ if .IsResourceType }}
{{ EnsureNamespace "" }}
{{- IfdefFuchsia -}}
void {{ . }}::_CloseHandles() {
  {{- range .Members }}
    {{- if .Type.IsResource }}
      if (has_{{ .Name }}()) {
        {{- CloseHandles . true false }}
      }
    {{- end }}
  {{- end }}
}
{{- EndifFuchsia -}}
{{- end }}
{{- end }}

{{/* TODO(fxbug.dev/36441): Remove __Fuchsia__ ifdefs once we have non-Fuchsia
     emulated handles for C++. */}}
{{- define "TableTraits" }}
{{ if .IsResourceType }}
{{- IfdefFuchsia -}}
{{- end }}
template <>
struct IsFidlType<{{ . }}> : public std::true_type {};
template <>
struct IsTable<{{ . }}> : public std::true_type {};
static_assert(std::is_standard_layout_v<{{ . }}>);
{{- if .IsResourceType }}
{{- EndifFuchsia -}}
{{- end }}
{{- end }}
`
