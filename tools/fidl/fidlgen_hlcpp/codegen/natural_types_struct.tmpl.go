// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const structTemplate = `
{{- define "StructForwardDeclaration" }}
{{ EnsureNamespace . }}
class {{ .Name }};
{{- end }}

{{/* TODO(fxbug.dev/36441): Remove __Fuchsia__ ifdefs once we have non-Fuchsia
     emulated handles for C++. */}}
{{- define "StructDeclaration" }}
{{ EnsureNamespace . }}
{{ if .IsResourceType }}
{{- IfdefFuchsia -}}
{{- end }}
{{- .Docs }}
class {{ .Name }} final {
 public:
  static const fidl_type_t* FidlType;

  {{- if .IsResultValue }}
  {{ .Name }}() = default;

  {{- if eq 1 (len .Members) }}
  explicit {{ .Name }}({{ (index .Members 0).Type }} v) : {{ (index .Members 0).Name }}(std::move(v)) {}
  {{ (index .Members 0).Type }} ResultValue_() { return std::move({{ (index .Members 0).Name }}); }
  {{- end }}
  explicit {{ .Name }}({{ .Result.ValueTupleDecl }} _value_tuple) {
    {{- if .Result.ValueArity }}
    std::tie(
      {{- range $index, $member := .Members }}
      {{- if $index }}, {{ end -}}
      {{ .Name }}
      {{- end -}}
    ) = std::move(_value_tuple);
    {{- end }}
  }
  operator {{ .Result.ValueTupleDecl }}() && {
    return std::make_tuple(
      {{- if .Result.ValueArity }}
        {{- range $index, $member := .Members }}
          {{- if $index }}, {{ end -}}
          std::move({{ .Name }})
        {{- end -}}
      {{- end }}
    );
  }
  {{- end }}

  {{- range .Members }}
  {{ .Docs }}
  {{ .Type }} {{ .Name }}{{ if .DefaultValue.IsSet }} = {{ .DefaultValue }}{{ else }}{}{{ end }};
  {{- end }}

  static inline ::std::unique_ptr<{{ .Name }}> New() { return ::std::make_unique<{{ .Name }}>(); }

  void Encode(::fidl::Encoder* _encoder, size_t _offset,
               cpp17::optional<::fidl::HandleInformation> maybe_handle_info = cpp17::nullopt);
  static void Decode(::fidl::Decoder* _decoder, {{ .Name }}* value, size_t _offset);
  zx_status_t Clone({{ .Name }}* result) const;
};

inline zx_status_t Clone(const {{ . }}& _value,
                         {{ . }}* _result) {
  return _value.Clone(_result);
}

using {{ .Name }}Ptr = ::std::unique_ptr<{{ .Name }}>;
{{- if .IsResourceType }}
{{- EndifFuchsia -}}
{{ end }}

{{- end }}

{{- define "StructDefinition" }}
{{ EnsureNamespace . }}
{{- if .IsResourceType }}
{{- IfdefFuchsia -}}
{{- end }}
extern "C" const fidl_type_t {{ .CodingTableType }};
const fidl_type_t* {{ .Name }}::FidlType = &{{ .CodingTableType }};

void {{ .Name }}::Encode(::fidl::Encoder* _encoder, size_t _offset,
                         cpp17::optional<::fidl::HandleInformation> maybe_handle_info) {
  if (::fidl::IsMemcpyCompatible<{{ .Name }}>::value) {
    memcpy(_encoder->template GetPtr<{{ .Name }}>(_offset), this, sizeof({{ .Name }}));
  } else {
    if (_encoder->wire_format() == FIDL_WIRE_FORMAT_VERSION_V1) {
      {{- range .Members }}
      {{- if .HandleInformation }}
      ::fidl::Encode(_encoder, &{{ .Name }}, _offset + {{ .OffsetV1 }}, ::fidl::HandleInformation {
        .object_type = {{ .HandleInformation.ObjectType }},
        .rights = {{ .HandleInformation.Rights }},
      });
      {{ else -}}
      ::fidl::Encode(_encoder, &{{ .Name }}, _offset + {{ .OffsetV1 }});
      {{ end -}}
      {{- end }}
    } else {
      {{- range .Members }}
      {{- if .HandleInformation }}
      ::fidl::Encode(_encoder, &{{ .Name }}, _offset + {{ .OffsetV2 }}, ::fidl::HandleInformation {
        .object_type = {{ .HandleInformation.ObjectType }},
        .rights = {{ .HandleInformation.Rights }},
      });
      {{ else -}}
      ::fidl::Encode(_encoder, &{{ .Name }}, _offset + {{ .OffsetV2 }});
      {{ end -}}
      {{- end }}
    }
  }
}

void {{ .Name }}::Decode(::fidl::Decoder* _decoder, {{ .Name }}* _value, size_t _offset) {
  if (::fidl::IsMemcpyCompatible<{{ .Name }}>::value) {
    memcpy(_value, _decoder->template GetPtr<{{ .Name }}>(_offset), sizeof({{ .Name }}));
  } else {
    {{- range .Members }}
    ::fidl::Decode(_decoder, &_value->{{ .Name }}, _offset + {{ .OffsetV1 }});
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
{{- EndifFuchsia -}}
{{ end }}

{{- end }}

{{- define "StructTraits" }}
{{- if .IsResourceType }}
{{- IfdefFuchsia -}}
{{- end }}
template <>
struct CodingTraits<{{ . }}>
    : public EncodableCodingTraits<{{ . }}, {{ .TypeShapeV1.InlineSize }}, {{ .TypeShapeV2.InlineSize }}> {};

{{ if .TypeShapeV1.HasPadding }}
template<>
struct HasPadding<{{ . }}> : public std::true_type {};
{{ end }}

{{ if .FullDeclMemcpyCompatibleDeps }}
template<>
struct IsMemcpyCompatible<{{ . }}> : public internal::BoolConstant<
    !HasPadding<{{ . }}>::value
{{- range .FullDeclMemcpyCompatibleDeps }}
    && IsMemcpyCompatible<{{ . }}>::value
  {{- end -}}
> {};
{{ end }}

inline zx_status_t Clone(const {{ . }}& value,
                         {{ . }}* result) {
  return {{ .Namespace }}::Clone(value, result);
}

template<>
struct Equality<{{ . }}> {
  bool operator()(const {{ . }}& _lhs, const {{ . }}& _rhs) const {
    {{- range $index, $member := .Members }}
    if (!::fidl::Equals(_lhs.{{ .Name }}, _rhs.{{ .Name }})) {
      return false;
    }
    {{- end }}
    return true;
  }
};
{{- if .IsResourceType }}
{{- EndifFuchsia -}}
{{ end }}

{{- end }}
`
