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
  {{ .Name }}() : ordinal_(Ordinal::Invalid), envelope_{} {}

  enum class Tag : fidl_xunion_tag_t {
  {{- range .Members }}
    {{ .TagName }} = {{ .Ordinal }},  // {{ .Ordinal | printf "%#x" }}
  {{- end }}
  {{- if .IsFlexible }}
    kUnknown = ::std::numeric_limits<::fidl_union_tag_t>::max(),
  {{- end }}
  };

  bool has_invalid_tag() const { return ordinal_ == Ordinal::Invalid; }

  {{- range $index, $member := .Members }}

  bool is_{{ .Name }}() const { return ordinal() == Ordinal::{{ .TagName }}; }

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
    ordinal_ = Ordinal::{{ .TagName }};
    envelope_.data = static_cast<void*>(elem);
  }
{{ "" }}
  {{- range .DocComments }}
  //{{ . }}
  {{- end }}
  {{ .Type.LLDecl }}& mutable_{{ .Name }}() {
    ZX_ASSERT(ordinal() == Ordinal::{{ .TagName }});
    return *static_cast<{{ .Type.LLDecl }}*>(envelope_.data);
  }
  const {{ .Type.LLDecl }}& {{ .Name }}() const {
    ZX_ASSERT(ordinal() == Ordinal::{{ .TagName }});
    return *static_cast<{{ .Type.LLDecl }}*>(envelope_.data);
  }
  {{- end }}

  {{- if .IsFlexible }}
  void* unknownData() const {
    ZX_ASSERT(which() == Tag::kUnknown);
    return envelope_.data;
  }
  {{- end }}
  {{- if .IsFlexible }}
  Tag which() const;
  {{- else }}
  Tag which() const {
    ZX_ASSERT(!has_invalid_tag());
    return static_cast<Tag>(ordinal());
  }
  {{- end }}

  static constexpr const fidl_type_t* Type = &{{ .V1TableType }};
  static constexpr const fidl_type_t* AltType = &{{ .TableType }};
  static constexpr uint32_t MaxNumHandles = {{ .MaxHandles }};
  static constexpr uint32_t PrimarySize = {{ .InlineSizeV1NoEE }};
  [[maybe_unused]]
  static constexpr uint32_t MaxOutOfLine = {{ .MaxOutOfLineV1NoEE }};
  static constexpr uint32_t AltPrimarySize = {{ .Size }};
  [[maybe_unused]]
  static constexpr uint32_t AltMaxOutOfLine = {{ .MaxOutOfLine }};

 private:
  enum class Ordinal : fidl_xunion_tag_t {
    Invalid = 0,
  {{- range .Members }}
    {{ .TagName }} = {{ .Ordinal }},  // {{ .Ordinal | printf "%#x" }}
  {{- end }}
  };

  Ordinal ordinal() const {
    {{- if .ShouldReadBothOrdinals }}
    switch (static_cast<fidl_xunion_tag_t>(ordinal_)) {
      {{- range .Members }}
      case {{ .ExplicitOrdinal }}:
      case {{ .HashedOrdinal }}:
        return Ordinal::{{ .TagName }};
      {{- end }}
    }
    {{- end }}
    return ordinal_;
  }

  static void SizeAndOffsetAssertionHelper();

  {{- /* All fields are private to maintain standard layout */}}
  Ordinal ordinal_;
  FIDL_ALIGNDECL
  fidl_envelope_t envelope_;
};
{{- end }}

{{- define "XUnionDefinition" }}

{{- if .IsFlexible }}
auto {{ .Namespace }}::{{ .Name }}::which() const -> Tag {
  ZX_ASSERT(!has_invalid_tag());
  switch (ordinal()) {
  {{- range .Members }}
  case Ordinal::{{ .TagName }}:
  {{- end }}
    return static_cast<Tag>(ordinal());
  default:
    return Tag::kUnknown;
  }
}
{{- end }}

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
