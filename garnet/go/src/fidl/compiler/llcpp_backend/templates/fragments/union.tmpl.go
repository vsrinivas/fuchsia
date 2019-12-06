// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fragments

const Union = `
{{- define "UnionForwardDeclaration" }}
struct {{ .Name }};
{{- end }}

{{- define "UnionDeclaration" }}
{{- $union := . }}

extern "C" const fidl_type_t {{ .TableType }};
extern "C" const fidl_type_t {{ .V1TableType }};
{{range .DocComments}}
//{{ . }}
{{- end}}
struct {{ .Name }} {
  enum class Tag : fidl_union_tag_t {
  {{- range $index, $member := .Members }}
    {{ $member.TagName }} = {{ $index }},
  {{- end }}
  };

  {{ .Name }}();
  ~{{ .Name }}();

  {{ .Name }}({{ .Name }}&& other) {
    ordinal_ = Ordinal::Invalid;
    if (this != &other) {
      MoveImpl_(std::move(other));
    }
  }

  {{ .Name }}& operator=({{ .Name }}&& other) {
    if (this != &other) {
      MoveImpl_(std::move(other));
    }
    return *this;
  }

  bool has_invalid_tag() const { return ordinal_ == Ordinal::Invalid; }

  {{- range $index, $member := .Members }}

  bool is_{{ .Name }}() const { return ordinal_ == Ordinal::{{ .TagName }}; }

  static {{ $.Name }} With{{ .UpperCamelCaseName }}({{ .Type.LLDecl }}* val) {
    {{ $.Name }} result;
    result.set_{{ .Name }}(val);
    return result;
  }
{{ "" }}
  {{- range .DocComments }}
  //{{ . }}
  {{- end }}
  {{ .Type.LLDecl }}& mutable_{{ .Name }}();
{{ "" }}
  {{- range .DocComments }}
  //{{ . }}
  {{- end }}
  template <typename T>
  std::enable_if_t<std::is_convertible<T, {{ .Type.LLDecl }}>::value && std::is_copy_assignable<T>::value>
  set_{{ .Name }}(const T* v) {
    mutable_{{ .Name }}() = *v;
  }
{{ "" }}
  {{- range .DocComments }}
  //{{ . }}
  {{- end }}
  template <typename T>
  std::enable_if_t<std::is_convertible<T, {{ .Type.LLDecl }}>::value && std::is_move_assignable<T>::value>
  set_{{ .Name }}(T* v) {
    mutable_{{ .Name }}() = std::move(*v);
  }
{{ "" }}
  {{- range .DocComments }}
  //{{ . }}
  {{- end }}
  {{ .Type.LLDecl }} const & {{ .Name }}() const { return {{ .StorageName }}; }
  {{- end }}

  Tag which() const {
    ZX_ASSERT(!has_invalid_tag());
    return static_cast<Tag>(ordinal_);
  }

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
  enum class Ordinal : fidl_union_tag_t {
  {{- range $index, $member := .Members }}
    {{ $member.TagName }} = {{ $index }},
  {{- end }}
    Invalid = ::std::numeric_limits<::fidl_union_tag_t>::max(),
  };

  void Destroy();
  void MoveImpl_({{ .Name }}&& other);
  static void SizeAndOffsetAssertionHelper();

  {{- /* All fields are private to maintain standard layout */}}
  Ordinal ordinal_;
  union {
  {{- range .Members }}
    {{ .Type.LLDecl }} {{ .StorageName }};
  {{- end }}
  };
};
{{- end }}

{{- define "UnionDefinition" }}
{{- $union := . }}

{{ .Namespace }}::{{ .Name }}::{{ .Name }}() {
  ordinal_ = Ordinal::Invalid;
}

{{ .Namespace }}::{{ .Name }}::~{{ .Name }}() {
  Destroy();
}

void {{ .Namespace }}::{{ .Name }}::Destroy() {
  switch (ordinal_) {
  {{- range $index, $member := .Members }}
  {{- if $member.Type.LLDtor }}
  case Ordinal::{{ $member.TagName }}:
    {{ $member.StorageName }}.{{ $member.Type.LLDtor }}();
    break;
  {{- end }}
  {{- end }}
  default:
    break;
  }
  ordinal_ = Ordinal::Invalid;
}

void {{ .Namespace }}::{{ .Name }}::MoveImpl_({{ .Name }}&& other) {
  switch (other.ordinal_) {
  {{- range $index, $member := .Members }}
  case Ordinal::{{ $member.TagName }}:
    mutable_{{ .Name }}() = std::move(other.mutable_{{ .Name }}());
    break;
  {{- end }}
  default:
    break;
  }
  other.Destroy();
}

void {{ .Namespace }}::{{ .Name }}::SizeAndOffsetAssertionHelper() {
  {{- range .Members }}
  static_assert(offsetof({{ $union.Namespace }}::{{ $union.Name }}, {{ .StorageName }}) == {{ .Offset }});
  {{- end }}
  static_assert(sizeof({{ $union.Namespace }}::{{ $union.Name }}) == {{ $union.Namespace }}::{{ $union.Name }}::PrimarySize);
}

{{ range $index, $member := .Members }}
{{ .Type.LLDecl }}& {{ $union.Namespace }}::{{ $union.Name }}::mutable_{{ .Name }}() {
  if (ordinal_ != Ordinal::{{ .TagName }}) {
    Destroy();
    new (&{{ .StorageName }}) {{ .Type.LLDecl }};
    ordinal_ = Ordinal::{{ .TagName }};
  }
  return {{ .StorageName }};
}
{{ "" }}
{{- end }}

{{- end }}

{{- define "UnionTraits" }}

template <>
struct IsFidlType<{{ .Namespace }}::{{ .Name }}> : public std::true_type {};
static_assert(std::is_standard_layout_v<{{ .Namespace }}::{{ .Name }}>);
{{- end }}
`
