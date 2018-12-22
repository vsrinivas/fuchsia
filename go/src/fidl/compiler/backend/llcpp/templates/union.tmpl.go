// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

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
    {{ .TagName }} = {{ $index }},
  {{- end }}
    Invalid = ::std::numeric_limits<::fidl_union_tag_t>::max(),
  };

  bool has_invalid_tag() const { return tag_ == Tag::Invalid; }

  {{- range $index, $member := .Members }}

  bool is_{{ .Name }}() const { return tag_ == Tag::{{ .TagName }}; }
{{ "" }}
  {{- range .DocComments }}
  //{{ . }}
  {{- end }}
  {{ .Type.LLDecl }}& mutable_{{ .Name }}() {
    tag_ = Tag::{{ .TagName }};
    return {{ .StorageName }};
  }
{{ "" }}
  {{- range .DocComments }}
  //{{ . }}
  {{- end }}
  const {{ .Type.LLDecl }}& {{ .Name }}() const { return {{ .StorageName }}; }
  {{- end }}

  Tag which() const { return tag_; }

  static constexpr const fidl_type_t* Type = &{{ .TableType }};
  static constexpr uint32_t MaxNumHandles = {{ .MaxHandles }};
  static constexpr uint32_t PrimarySize = {{ .Size }};
  [[maybe_unused]]
  static constexpr uint32_t MaxOutOfLine = {{ .MaxOutOfLine }};

 private:
  void SizeAndOffsetAssertionHelper();

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

void {{ .Namespace }}::{{ .Name }}::SizeAndOffsetAssertionHelper() {
  {{- $union := . -}}
  {{- range .Members }}
  static_assert(offsetof({{ $union.Namespace }}::{{ $union.Name }}, {{ .StorageName }}) == {{ .Offset }});
  {{- end }}
  static_assert(sizeof({{ $union.Namespace }}::{{ $union.Name }}) == {{ $union.Namespace }}::{{ $union.Name }}::PrimarySize);
}
{{- end }}

{{- define "UnionTraits" }}

static_assert(std::is_standard_layout_v<{{ .Namespace }}::{{ .Name }}>);
{{- end }}
`
