// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fragments

const Table = `
{{- define "TableForwardDeclaration" }}
struct {{ .Name }};
{{- end }}

{{- define "TableDeclaration" }}

extern "C" const fidl_type_t {{ .TableType }};
extern "C" const fidl_type_t {{ .V1TableType }};
{{ range .DocComments }}
//{{ . }}
{{- end}}
struct {{ .Name }} final : private ::fidl::VectorView<fidl_envelope_t> {
  using EnvelopesView = ::fidl::VectorView<fidl_envelope_t>;
 public:
  // Returns whether no field is set.
  bool IsEmpty() const { return EnvelopesView::empty(); }
  {{- range .Members }}
{{ "" }}
    {{- range .DocComments }}
  //{{ . }}
    {{- end }}
  const {{ .Type.LLDecl }}& {{ .Name }}() const {
    ZX_ASSERT({{ .MethodHasName }}());
    return *reinterpret_cast<const {{ .Type.LLDecl }}*>(EnvelopesView::at({{ .Ordinal }} - 1).data);
  }
  {{ .Type.LLDecl }}& {{ .Name }}() {
    ZX_ASSERT({{ .MethodHasName }}());
    return *reinterpret_cast<{{ .Type.LLDecl }}*>(EnvelopesView::at({{ .Ordinal }} - 1).data);
  }
  bool {{ .MethodHasName }}() const {
    return EnvelopesView::count() >= {{ .Ordinal }} && EnvelopesView::at({{ .Ordinal }} - 1).data != nullptr;
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
  static constexpr const fidl_type_t* AltType = &{{ .V1TableType }};
  static constexpr uint32_t MaxNumHandles = {{ .MaxHandles }};
  static constexpr uint32_t PrimarySize = {{ .Size }};
  [[maybe_unused]]
  static constexpr uint32_t MaxOutOfLine = {{ .MaxOutOfLine }};
  static constexpr uint32_t AltPrimarySize = {{ .InlineSizeV1NoEE }};
  [[maybe_unused]]
  static constexpr uint32_t AltMaxOutOfLine = {{ .MaxOutOfLineV1NoEE }};

 private:
  {{ .Name }}(uint64_t max_ordinal, fidl_envelope_t* data) : EnvelopesView(data, max_ordinal) {}
};

class {{ .Name }}::Builder {
 public:
  {{- if eq .BiggestOrdinal 0 }}
  {{- /* Zero-sized arrays are questionable in C++ */}}
  {{ .Name }} view() { return {{ .Name }}(0, nullptr); }
  {{- else }}
  {{ .Name }} view() { return {{ .Name }}(max_ordinal_, envelopes_.data_); }
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
  ::fidl::Array<fidl_envelope_t, {{ .BiggestOrdinal }}> envelopes_ = {};
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
  envelopes_[{{ .Ordinal }} - 1].data = static_cast<void*>(elem);
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
