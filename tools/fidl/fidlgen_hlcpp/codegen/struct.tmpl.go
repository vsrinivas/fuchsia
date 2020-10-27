// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const structTemplate = `
{{- define "StructForwardDeclaration" }}
class {{ .Name }};
{{- end }}

{{/* TODO(fxbug.dev/36441): Remove __Fuchsia__ ifdefs once we have non-Fuchsia
     emulated handles for C++. */}}
{{- define "StructDeclaration" }}
{{ if .IsResourceType }}
#ifdef __Fuchsia__
{{- end }}
{{- range .DocComments }}
///{{ . }}
{{- end }}
class {{ .Name }} final {
 public:
  static const fidl_type_t* FidlType;

  {{- if .IsResultValue }}
  {{ .Name }}() = default;

  {{- if eq 1 (len .Members) }}
  explicit {{ .Name }}({{ (index .Members 0).Type.Decl }} v) : {{ (index .Members 0).Name }}(std::move(v)) {}
  {{ (index .Members 0).Type.Decl }} ResultValue_() { return std::move({{ (index .Members 0).Name }}); }
  {{- end }}
  explicit {{ .Name }}({{ .Result.ValueTupleDecl }} _value_tuple) {
    {{- if .Result.ValueArity }}
    std::tie(
      {{- range $index, $member := .Members }}
      {{- if $index }}, {{ end -}}
      {{.Name}}
      {{- end -}}
    ) = std::move(_value_tuple);
    {{- end }}
  }
  operator {{ .Result.ValueTupleDecl }}() && {
    return std::make_tuple(
      {{- if .Result.ValueArity }}
        {{- range $index, $member := .Members }}
          {{- if $index }}, {{ end -}}
          std::move({{.Name}})
        {{- end -}}
      {{- end }}
    );
  }
  {{- end }}

  {{- range .Members }}
  {{range .DocComments}}
  ///{{ . }}
  {{- end}}
  {{ .Type.Decl }} {{ .Name }}{{ if .DefaultValue }} = {{ .DefaultValue }}{{ else }}{}{{ end }};
  {{- end }}

  static inline ::std::unique_ptr<{{ .Name }}> New() { return ::std::make_unique<{{ .Name }}>(); }

  void Encode(::fidl::Encoder* _encoder, size_t _offset);
  static void Decode(::fidl::Decoder* _decoder, {{ .Name }}* value, size_t _offset);
  zx_status_t Clone({{ .Name }}* result) const;
};

inline zx_status_t Clone(const {{ .Namespace }}::{{ .Name }}& _value,
                         {{ .Namespace }}::{{ .Name }}* _result) {
  return _value.Clone(_result);
}

using {{ .Name }}Ptr = ::std::unique_ptr<{{ .Name }}>;
{{- if .IsResourceType }}
#endif  // __Fuchsia__
{{ end }}

{{- end }}

{{- define "StructDefinition" }}
{{- if .IsResourceType }}
#ifdef __Fuchsia__
{{- end }}
extern "C" const fidl_type_t {{ .TableType }};
const fidl_type_t* {{ .Name }}::FidlType = &{{ .TableType }};

void {{ .Name }}::Encode(::fidl::Encoder* _encoder, size_t _offset) {
  if (::fidl::IsMemcpyCompatible<{{ .Name }}>::value) {
    memcpy(_encoder->template GetPtr<{{ .Name }}>(_offset), this, sizeof({{ .Name }}));
  } else {
    {{- range .Members }}
    ::fidl::Encode(_encoder, &{{ .Name }}, _offset + {{ .Offset }});
    {{- end }}
  }
}

void {{ .Name }}::Decode(::fidl::Decoder* _decoder, {{ .Name }}* _value, size_t _offset) {
  if (::fidl::IsMemcpyCompatible<{{ .Name }}>::value) {
    memcpy(_value, _decoder->template GetPtr<{{ .Name }}>(_offset), sizeof({{ .Name }}));
  } else {
    {{- range .Members }}
    ::fidl::Decode(_decoder, &_value->{{ .Name }}, _offset + {{ .Offset }});
    {{- end }}
  }
}

zx_status_t {{ .Name }}::Clone({{ .Name }}* _result) const {
  {{- range $index, $member := .Members }}
  {{ if not $index }}zx_status_t {{ end -}}
  _status = ::fidl::Clone({{ .Name }}, &_result->{{ .Name }});
  if (_status != ZX_OK)
    return _status;
  {{- end }}
  return ZX_OK;
}
{{- if .IsResourceType }}
#endif  // __Fuchsia__
{{ end }}

{{- end }}

{{- define "StructTraits" }}
{{- if .IsResourceType }}
#ifdef __Fuchsia__
{{- end }}
template <>
struct CodingTraits<{{ .Namespace }}::{{ .Name }}>
    : public EncodableCodingTraits<{{ .Namespace }}::{{ .Name }}, {{ .InlineSize }}> {};

{{ if .HasPadding }}
template<>
struct HasPadding<{{ .Namespace }}::{{ .Name }}> : public std::true_type {};
{{ end }}

{{ if .FullDeclMemcpyCompatibleDeps }}
template<>
struct IsMemcpyCompatible<{{ .Namespace }}::{{ .Name }}> : public internal::BoolConstant<
    !HasPadding<{{ .Namespace }}::{{ .Name }}>::value
{{- range .FullDeclMemcpyCompatibleDeps }}
    && IsMemcpyCompatible<{{ . }}>::value
  {{- end -}}
> {};
{{ end }}

inline zx_status_t Clone(const {{ .Namespace }}::{{ .Name }}& value,
                         {{ .Namespace }}::{{ .Name }}* result) {
  return {{ .Namespace }}::Clone(value, result);
}

template<>
struct Equality<{{ .Namespace }}::{{ .Name }}> {
  bool operator()(const {{ .Namespace }}::{{ .Name }}& _lhs, const {{ .Namespace }}::{{ .Name }}& _rhs) const {
    {{- range $index, $member := .Members }}
    if (!::fidl::Equals(_lhs.{{ .Name }}, _rhs.{{ .Name }})) {
      return false;
    }
    {{- end }}
    return true;
  }
};
{{- if .IsResourceType }}
#endif  // __Fuchsia__
{{ end }}

{{- end }}
`
