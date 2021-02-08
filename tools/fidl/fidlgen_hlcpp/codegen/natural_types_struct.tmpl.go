// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const structTemplate = `
{{- define "StructForwardDeclaration" }}
{{ EnsureNamespace .Decl.Natural }}
class {{ .Decl.Natural.Name }};
{{- end }}

{{/* TODO(fxbug.dev/36441): Remove __Fuchsia__ ifdefs once we have non-Fuchsia
     emulated handles for C++. */}}
{{- define "StructDeclaration" }}
{{ EnsureNamespace .Decl.Natural }}
{{ if .IsResourceType }}
#ifdef __Fuchsia__
{{- PushNamespace }}
{{- end }}
{{- range .DocComments }}
///{{ . }}
{{- end }}
class {{ .Decl.Natural.Name }} final {
 public:
  static const fidl_type_t* FidlType;

  {{- if .IsResultValue }}
  {{ .Decl.Natural.Name }}() = default;

  {{- if eq 1 (len .Members) }}
  explicit {{ .Decl.Natural.Name }}({{ (index .Members 0).Type.Natural }} v) : {{ (index .Members 0).Name }}(std::move(v)) {}
  {{ (index .Members 0).Type.Natural }} ResultValue_() { return std::move({{ (index .Members 0).Name }}); }
  {{- end }}
  explicit {{ .Decl.Natural.Name }}({{ .Result.ValueTupleDecl }} _value_tuple) {
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
  {{ .Type.Natural }} {{ .Name }}{{ if .DefaultValue.Natural }} = {{ .DefaultValue.Natural }}{{ else }}{}{{ end }};
  {{- end }}

  static inline ::std::unique_ptr<{{ .Decl.Natural.Name }}> New() { return ::std::make_unique<{{ .Decl.Natural.Name }}>(); }

  void Encode(::fidl::Encoder* _encoder, size_t _offset,
               fit::optional<::fidl::HandleInformation> maybe_handle_info = fit::nullopt);
  static void Decode(::fidl::Decoder* _decoder, {{ .Decl.Natural.Name }}* value, size_t _offset);
  zx_status_t Clone({{ .Decl.Natural.Name }}* result) const;
};

inline zx_status_t Clone(const {{ .Decl.Natural }}& _value,
                         {{ .Decl.Natural }}* _result) {
  return _value.Clone(_result);
}

using {{ .Decl.Natural.Name }}Ptr = ::std::unique_ptr<{{ .Decl.Natural.Name }}>;
{{- if .IsResourceType }}
{{- PopNamespace }}
#endif  // __Fuchsia__
{{ end }}

{{- end }}

{{- define "StructDefinition" }}
{{ EnsureNamespace .Decl.Natural }}
{{- if .IsResourceType }}
#ifdef __Fuchsia__
{{- PushNamespace }}
{{- end }}
extern "C" const fidl_type_t {{ .TableType }};
const fidl_type_t* {{ .Decl.Natural.Name }}::FidlType = &{{ .TableType }};

void {{ .Decl.Natural.Name }}::Encode(::fidl::Encoder* _encoder, size_t _offset,
                         fit::optional<::fidl::HandleInformation> maybe_handle_info) {
  if (::fidl::IsMemcpyCompatible<{{ .Decl.Natural.Name }}>::value) {
    memcpy(_encoder->template GetPtr<{{ .Decl.Natural.Name }}>(_offset), this, sizeof({{ .Decl.Natural.Name }}));
  } else {
    {{- range .Members }}
    {{- if .HandleInformation }}
    ::fidl::Encode(_encoder, &{{ .Name }}, _offset + {{ .Offset }}, ::fidl::HandleInformation {
      .object_type = {{ .HandleInformation.ObjectType }},
      .rights = {{ .HandleInformation.Rights }},
    });
    {{ else -}}
    ::fidl::Encode(_encoder, &{{ .Name }}, _offset + {{ .Offset }});
    {{ end -}}
    {{- end }}
  }
}

void {{ .Decl.Natural.Name }}::Decode(::fidl::Decoder* _decoder, {{ .Decl.Natural.Name }}* _value, size_t _offset) {
  if (::fidl::IsMemcpyCompatible<{{ .Decl.Natural.Name }}>::value) {
    memcpy(_value, _decoder->template GetPtr<{{ .Decl.Natural.Name }}>(_offset), sizeof({{ .Decl.Natural.Name }}));
  } else {
    {{- range .Members }}
    ::fidl::Decode(_decoder, &_value->{{ .Name }}, _offset + {{ .Offset }});
    {{- end }}
  }
}

zx_status_t {{ .Decl.Natural.Name }}::Clone({{ .Decl.Natural.Name }}* _result) const {
  {{- range $index, $member := .Members }}
  {{ if not $index }}zx_status_t {{ end -}}
  _status = ::fidl::Clone({{ .Name }}, &_result->{{ .Name }});
  if (_status != ZX_OK)
    return _status;
  {{- end }}
  return ZX_OK;
}
{{- if .IsResourceType }}
{{- PopNamespace }}
#endif  // __Fuchsia__
{{ end }}

{{- end }}

{{- define "StructTraits" }}
{{- if .IsResourceType }}
#ifdef __Fuchsia__
{{- PushNamespace }}
{{- end }}
template <>
struct CodingTraits<{{ .Decl.Natural }}>
    : public EncodableCodingTraits<{{ .Decl.Natural }}, {{ .InlineSize }}> {};

{{ if .HasPadding }}
template<>
struct HasPadding<{{ .Decl.Natural }}> : public std::true_type {};
{{ end }}

{{ if .FullDeclMemcpyCompatibleDeps }}
template<>
struct IsMemcpyCompatible<{{ .Decl.Natural }}> : public internal::BoolConstant<
    !HasPadding<{{ .Decl.Natural }}>::value
{{- range .FullDeclMemcpyCompatibleDeps }}
    && IsMemcpyCompatible<{{ . }}>::value
  {{- end -}}
> {};
{{ end }}

inline zx_status_t Clone(const {{ .Decl.Natural }}& value,
                         {{ .Decl.Natural }}* result) {
  return {{ .Decl.Natural.Namespace }}::Clone(value, result);
}

template<>
struct Equality<{{ .Decl.Natural }}> {
  bool operator()(const {{ .Decl.Natural }}& _lhs, const {{ .Decl.Natural }}& _rhs) const {
    {{- range $index, $member := .Members }}
    if (!::fidl::Equals(_lhs.{{ .Name }}, _rhs.{{ .Name }})) {
      return false;
    }
    {{- end }}
    return true;
  }
};
{{- if .IsResourceType }}
{{- PopNamespace }}
#endif  // __Fuchsia__
{{ end }}

{{- end }}
`
