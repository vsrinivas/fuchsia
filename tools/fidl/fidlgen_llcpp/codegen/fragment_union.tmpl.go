// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentUnionTmpl = `
{{- define "UnionForwardDeclaration" }}
class {{ .Name }};
{{- end }}

{{- define "UnionDeclaration" }}

extern "C" const fidl_type_t {{ .TableType }};
{{range .DocComments}}
//{{ . }}
{{- end}}
class {{ .Name }} {
  public:
  {{ .Name }}() : ordinal_(Ordinal::Invalid), envelope_{} {}

  {{ .Name }}({{ .Name }}&&) = default;
  {{ .Name }}& operator=({{ .Name }}&&) = default;

  ~{{ .Name }}() {
    reset_ptr(nullptr);
  }

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

  bool is_{{ .Name }}() const { return ordinal_ == Ordinal::{{ .TagName }}; }

  static {{ $.Name }} With{{ .UpperCamelCaseName }}(::fidl::tracking_ptr<{{ .Type.LLDecl }}>&& val) {
    {{ $.Name }} result;
    result.set_{{ .Name }}(std::move(val));
    return result;
  }
{{ "" }}
  {{- range .DocComments }}
  //{{ . }}
  {{- end }}
  void set_{{ .Name }}(::fidl::tracking_ptr<{{ .Type.LLDecl }}>&& elem) {
    ordinal_ = Ordinal::{{ .TagName }};
    reset_ptr(static_cast<::fidl::tracking_ptr<void>>(std::move(elem)));
  }
{{ "" }}
  {{- range .DocComments }}
  //{{ . }}
  {{- end }}
  {{ .Type.LLDecl }}& mutable_{{ .Name }}() {
    ZX_ASSERT(ordinal_ == Ordinal::{{ .TagName }});
    return *static_cast<{{ .Type.LLDecl }}*>(envelope_.data.get());
  }
  const {{ .Type.LLDecl }}& {{ .Name }}() const {
    ZX_ASSERT(ordinal_ == Ordinal::{{ .TagName }});
    return *static_cast<{{ .Type.LLDecl }}*>(envelope_.data.get());
  }
  {{- end }}

  {{- if .IsFlexible }}
  Tag which() const;
  {{- else }}
  Tag which() const {
    ZX_ASSERT(!has_invalid_tag());
    return static_cast<Tag>(ordinal_);
  }
  {{- end }}

  static constexpr const fidl_type_t* Type = &{{ .TableType }};
  static constexpr uint32_t MaxNumHandles = {{ .MaxHandles }};
  static constexpr uint32_t PrimarySize = {{ .InlineSize }};
  [[maybe_unused]]
  static constexpr uint32_t MaxOutOfLine = {{ .MaxOutOfLine }};
  static constexpr bool HasPointer = {{ .HasPointer }};
  static constexpr bool IsResource = {{ .IsResource }};

 private:
  enum class Ordinal : fidl_xunion_tag_t {
    Invalid = 0,
  {{- range .Members }}
    {{ .TagName }} = {{ .Ordinal }},  // {{ .Ordinal | printf "%#x" }}
  {{- end }}
  };

  void reset_ptr(::fidl::tracking_ptr<void>&& new_ptr) {
    // To clear the existing value, std::move it and let it go out of scope.
    switch (static_cast<fidl_xunion_tag_t>(ordinal_)) {
    {{- range .Members }}
    case {{ .Ordinal }}: {
      ::fidl::tracking_ptr<{{.Type.LLDecl}}> to_destroy =
        static_cast<::fidl::tracking_ptr<{{.Type.LLDecl}}>>(std::move(envelope_.data));
      break;
    }
    {{- end}}
    }

    envelope_.data = std::move(new_ptr);
  }

  static void SizeAndOffsetAssertionHelper();

  {{- /* All fields are private to maintain standard layout */}}
  Ordinal ordinal_;
  FIDL_ALIGNDECL
  ::fidl::Envelope<void> envelope_;
};
{{- end }}

{{- define "UnionDefinition" }}

{{- if .IsFlexible }}
auto {{ .Namespace }}::{{ .Name }}::which() const -> Tag {
  ZX_ASSERT(!has_invalid_tag());
  switch (ordinal_) {
  {{- range .Members }}
  case Ordinal::{{ .TagName }}:
  {{- end }}
    return static_cast<Tag>(ordinal_);
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

{{- define "UnionTraits" }}

template <>
struct IsFidlType<{{ .Namespace }}::{{ .Name }}> : public std::true_type {};
template <>
struct IsUnion<{{ .Namespace }}::{{ .Name }}> : public std::true_type {};
static_assert(std::is_standard_layout_v<{{ .Namespace }}::{{ .Name }}>);
{{- end }}
`
