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
    Invalid = ::std::numeric_limits<::fidl_union_tag_t>::max(),
  };

  {{ .Name }}();
  ~{{ .Name }}();

  {{ .Name }}({{ .Name }}&& other) {
    tag_ = Tag::Invalid;
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

  bool has_invalid_tag() const { return tag_ == Tag::Invalid; }

  {{- range $index, $member := .Members }}

  bool is_{{ .Name }}() const { return tag_ == Tag::{{ .TagName }}; }

  // TODO(fxb/41475) Remove this in favor of the pointer version.
  static {{ $.Name }} With{{ .UpperCamelCaseName }}({{ .Type.LLDecl }}&& val) {
    {{ $.Name }} result;
    result.set_{{ .Name }}(std::move(val));
    return result;
  }
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
  // TODO(fxb/41475) Remove this in favor of the pointer version.
  template <typename T>
  std::enable_if_t<std::is_convertible<T, {{ .Type.LLDecl }}>::value && std::is_copy_assignable<T>::value>
  set_{{ .Name }}(const T& v) {
    mutable_{{ .Name }}() = v;
  }
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
  // TODO(fxb/41475) Remove this in favor of the pointer version.
  template <typename T>
  std::enable_if_t<std::is_convertible<T, {{ .Type.LLDecl }}>::value && std::is_move_assignable<T>::value>
  set_{{ .Name }}(T&& v) {
    mutable_{{ .Name }}() = std::move(v);
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

  Tag which() const { return tag_; }

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
  void Destroy();
  void MoveImpl_({{ .Name }}&& other);
  static void SizeAndOffsetAssertionHelper();

  {{- /* All fields are private to maintain standard layout */}}
  Tag tag_;
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
  tag_ = Tag::Invalid;
}

{{ .Namespace }}::{{ .Name }}::~{{ .Name }}() {
  Destroy();
}

void {{ .Namespace }}::{{ .Name }}::Destroy() {
  switch (which()) {
  {{- range $index, $member := .Members }}
  {{- if $member.Type.LLDtor }}
  case Tag::{{ $member.TagName }}:
    {{ $member.StorageName }}.{{ $member.Type.LLDtor }}();
    break;
  {{- end }}
  {{- end }}
  default:
    break;
  }
  tag_ = Tag::Invalid;
}

void {{ .Namespace }}::{{ .Name }}::MoveImpl_({{ .Name }}&& other) {
  switch (other.which()) {
  {{- range $index, $member := .Members }}
  case Tag::{{ $member.TagName }}:
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
  if (which() != Tag::{{ .TagName }}) {
    Destroy();
    new (&{{ .StorageName }}) {{ .Type.LLDecl }};
  }
  tag_ = Tag::{{ .TagName }};
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
