// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fragments

const Table = `
{{- define "TableForwardDeclaration" }}
class {{ .Name }};
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
    return *frame_->{{ .Name }}.data;
  }
  {{ .Type.LLDecl }}& {{ .Name }}() {
    ZX_ASSERT({{ .MethodHasName }}());
    return *frame_->{{ .Name }}.data;
  }
  bool {{ .MethodHasName }}() const {
    return max_ordinal_ >= {{ .Ordinal }} && frame_->{{ .Name }}.data != nullptr;
  }
  {{- end }}

  {{ .Name }}() = default;
  ~{{ .Name }}() = default;
  {{ .Name }}({{ .Name }}&& other) noexcept = default;
  {{ .Name }}& operator=({{ .Name }}&& other) noexcept = default;

  class Builder;
  friend class Builder;
  static Builder Build();
  static constexpr const fidl_type_t* Type = &{{ .TableType }};
  static constexpr uint32_t MaxNumHandles = {{ .MaxHandles }};
  static constexpr uint32_t PrimarySize = {{ .InlineSize }};
  [[maybe_unused]]
  static constexpr uint32_t MaxOutOfLine = {{ .MaxOutOfLine }};
  static constexpr bool HasPointer = {{ .HasPointer }};

  class Frame {
    {{- range .FrameItems }}
    ::fidl::Envelope<{{ .LLDecl }}> {{ .Name }};
    {{- end }}

    friend class {{ .Name }};
    friend class {{ .Name }}::Builder;
  };

 private:
  {{ .Name }}(uint64_t max_ordinal, ::fidl::tracking_ptr<Frame> && frame) : max_ordinal_(max_ordinal), frame_(std::move(frame)) {}
  uint64_t max_ordinal_;
  ::fidl::tracking_ptr<Frame> frame_;
};

class {{ .Name }}::Builder {
 public:
  {{- if eq .BiggestOrdinal 0 }}
  {{- /* Zero-sized arrays are questionable in C++ */}}
  {{ .Name }} view() { return {{ .Name }}(0, nullptr); }
  {{- else }}
  {{ .Name }} view() { return {{ .Name }}(max_ordinal_, ::fidl::unowned_ptr<{{.Name}}::Frame>(&frame_)); }
  {{- end }}
  ~Builder() = default;
  Builder(Builder&& other) noexcept = default;
  Builder& operator=(Builder&& other) noexcept = default;

  {{- range .Members }}
{{ "" }}
    {{- range .DocComments }}
  //{{ . }}
    {{- end }}
    {{- /* TODO(FIDL-677): The elem pointer should be const if it has no handles. */}}
  Builder&& set_{{ .Name }}({{ .Type.LLDecl }}* elem);
  {{- end }}

 private:
  Builder() = default;
  friend Builder {{ .Name }}::Build();

  {{- if eq .BiggestOrdinal 0 }}
  {{- /* Zero-sized arrays are questionable in C++ */}}
  {{- else }}

  uint64_t max_ordinal_ = 0;
  {{ .Name }}::Frame frame_ = {};
  {{- end }}
};
{{- end }}

{{- define "TableDefinition" }}
{{- $table := . }}

{{ .Namespace }}::{{ .Name }}::Builder {{ .Name }}::Build() {
  return {{ .Name }}::Builder();
}

{{- range .Members }}
{{ "" }}
auto {{ $table.Namespace }}::{{ $table.Name }}::Builder::set_{{ .Name }}({{ .Type.LLDecl }}* elem) -> Builder&& {
  ZX_ASSERT(elem);
  frame_.{{ .Name }}.data = ::fidl::unowned_ptr<{{ .Type.LLDecl }}>(elem);
  if (max_ordinal_ < {{ .Ordinal }}) {
    max_ordinal_ = {{ .Ordinal }};
  }
  return std::move(*this);
}
{{- end }}
{{- end }}

{{- define "TableTraits" }}

template <>
struct IsFidlType<{{ .Namespace }}::{{ .Name }}> : public std::true_type {};
static_assert(std::is_standard_layout_v<{{ .Namespace }}::{{ .Name }}>);
{{- end }}
`
