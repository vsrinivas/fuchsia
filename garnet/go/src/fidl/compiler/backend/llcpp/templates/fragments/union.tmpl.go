// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fragments

const Union = `
{{- define "UnionForwardDeclaration" }}
struct {{ .Name }};
{{- end }}

{{- define "UnionDeclaration" }}

extern "C" const fidl_type_t {{ .TableType }};
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

  bool has_invalid_tag() const { return tag_ == Tag::Invalid; }

  {{- range $index, $member := .Members }}

  bool is_{{ .Name }}() const { return tag_ == Tag::{{ .TagName }}; }
{{ "" }}
  {{- range .DocComments }}
  //{{ . }}
  {{- end }}
  {{ .Type.LLDecl }}& mutable_{{ .Name }}() {
    if (which() != Tag::{{ .TagName }}) {
      Destroy();
    }
    tag_ = Tag::{{ .TagName }};
    return {{ .StorageName }};
  }
{{ "" }}
  {{- range .DocComments }}
  //{{ . }}
  {{- end }}
  void set_{{ .Name }}({{ .Type.LLDecl }} const & v) {
    mutable_{{ .Name }}() = v;
  }
{{ "" }}
  {{- range .DocComments }}
  //{{ . }}
  {{- end }}
  {{ .Type.LLDecl }} const & {{ .Name }}() const { return {{ .StorageName }}; }
  {{- end }}

  Tag which() const { return tag_; }

  static constexpr const fidl_type_t* Type = &{{ .TableType }};
  static constexpr uint32_t MaxNumHandles = {{ .MaxHandles }};
  static constexpr uint32_t PrimarySize = {{ .Size }};
  [[maybe_unused]]
  static constexpr uint32_t MaxOutOfLine = {{ .MaxOutOfLine }};

 private:
  void Destroy();
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

{{ .Namespace }}::{{ .Name }}::{{ .Name }}() {
  memset(this, 0, sizeof({{ .Name }}));
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
}

void {{ .Namespace }}::{{ .Name }}::SizeAndOffsetAssertionHelper() {
  {{- $union := . -}}
  {{- range .Members }}
  static_assert(offsetof({{ $union.Namespace }}::{{ $union.Name }}, {{ .StorageName }}) == {{ .Offset }});
  {{- end }}
  static_assert(sizeof({{ $union.Namespace }}::{{ $union.Name }}) == {{ $union.Namespace }}::{{ $union.Name }}::PrimarySize);
}
{{- end }}

{{- define "UnionTraits" }}

template <>
struct IsFidlType<{{ .Namespace }}::{{ .Name }}> : public std::true_type {};
static_assert(std::is_standard_layout_v<{{ .Namespace }}::{{ .Name }}>);
{{- end }}
`
