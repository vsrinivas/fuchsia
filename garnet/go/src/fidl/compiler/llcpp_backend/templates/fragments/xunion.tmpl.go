// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fragments

const XUnion = `
{{- define "XUnionForwardDeclaration" }}
struct {{ .Name }};
{{- end }}

{{- define "XUnionDeclaration" }}

extern "C" const fidl_type_t {{ .TableType }};
extern "C" const fidl_type_t {{ .V1TableType }};
{{range .DocComments}}
//{{ . }}
{{- end}}
struct {{ .Name }} {
  {{ .Name }}() : ordinal_(Tag::kUnknown), envelope_{} {}

  enum class Tag : fidl_xunion_tag_t {
    kUnknown = 0,
  {{- range .Members }}
    {{ .TagName }} = {{ .Ordinal }},  // {{ .Ordinal | printf "%#x" }}
  {{- end }}
  };

  {{- range $index, $member := .Members }}

  bool is_{{ .Name }}() const { return ordinal_ == Tag::{{ .TagName }}; }

  static {{ $.Name }} With{{ .UpperCamelCaseName }}({{ .Type.LLDecl }}* val) {
    {{ $.Name }} result;
    result.set_{{ .Name }}(val);
    return result;
  }
{{ "" }}
  {{- range .DocComments }}
  //{{ . }}
  {{- end }}
  void set_{{ .Name }}({{ .Type.LLDecl }}* elem) {
    ordinal_ = Tag::{{ .TagName }};
    envelope_.data = static_cast<void*>(elem);
  }
{{ "" }}
  {{- range .DocComments }}
  //{{ . }}
  {{- end }}
  {{ .Type.LLDecl }}& mutable_{{ .Name }}() {
    ZX_ASSERT(ordinal_ == Tag::{{ .TagName }});
    return *static_cast<{{ .Type.LLDecl }}*>(envelope_.data);
  }
  const {{ .Type.LLDecl }}& {{ .Name }}() const {
    ZX_ASSERT(ordinal_ == Tag::{{ .TagName }});
    return *static_cast<{{ .Type.LLDecl }}*>(envelope_.data);
  }
  {{- end }}

  Tag which() const;

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
  static void SizeAndOffsetAssertionHelper();

  {{- /* All fields are private to maintain standard layout */}}
  Tag ordinal_;
  FIDL_ALIGNDECL
  fidl_envelope_t envelope_;
};
{{- end }}

{{- define "XUnionDefinition" }}

auto {{ .Namespace }}::{{ .Name }}::which() const -> Tag {
  switch (ordinal_) {
  {{- range .Members }}
  case Tag::{{ .TagName }}:
  {{- end }}
    return ordinal_;
  default:
    return Tag::kUnknown;
  }
}

void {{ .Namespace }}::{{ .Name }}::SizeAndOffsetAssertionHelper() {
  {{ $union := . -}}
  static_assert(sizeof({{ .Name }}) == sizeof(fidl_xunion_t));
  static_assert(offsetof({{ .Name }}, ordinal_) == offsetof(fidl_xunion_t, tag));
  static_assert(offsetof({{ .Name }}, envelope_) == offsetof(fidl_xunion_t, envelope));
}
{{- end }}

{{- define "XUnionTraits" }}

template <>
struct IsFidlType<{{ .Namespace }}::{{ .Name }}> : public std::true_type {};
static_assert(std::is_standard_layout_v<{{ .Namespace }}::{{ .Name }}>);
{{- end }}
`
