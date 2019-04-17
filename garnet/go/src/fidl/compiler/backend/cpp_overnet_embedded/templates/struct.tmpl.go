// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Struct = `
{{- define "StructForwardDeclaration" }}
class {{ .Name }};
{{- end }}

{{- define "StructDeclaration" }}
{{range .DocComments}}
//{{ . }}
{{- end}}
class {{ .Name }}  {
 public:
  static const fidl_type_t* FidlType;
  {{- range .Members }}
  {{range .DocComments}}
  //{{ . }}
  {{- end}}
  {{ .Type.OvernetEmbeddedDecl }} {{ .Name }}{{ if .DefaultValue }} = {{ .DefaultValue }}{{ else }}{}{{ end }};
  {{- end }}

  static inline ::std::unique_ptr<{{ .Name }}> New() { return ::std::make_unique<{{ .Name }}>(); }

  void Encode(::overnet::internal::Encoder* _encoder, size_t _offset);
  static void Decode(::overnet::internal::Decoder* _decoder, {{ .Name }}* value, size_t _offset);
  zx_status_t Clone({{ .Name }}* result) const;
};

#ifdef FIDL_OPERATOR_EQUALS
bool operator==(const {{ .Name }}& _lhs, const {{ .Name }}& _rhs);
inline bool operator!=(const {{ .Name }}& _lhs, const {{ .Name }}& _rhs) {
  return !(_lhs == _rhs);
}
#endif

inline zx_status_t Clone(const {{ .Namespace }}::embedded::{{ .Name }}& _value,
                         {{ .Namespace }}::embedded::{{ .Name }}* _result) {
  return _value.Clone(_result);
}

using {{ .Name }}Ptr = ::std::unique_ptr<{{ .Name }}>;
{{- end }}

{{- define "StructDefinition" }}
extern "C" const fidl_type_t {{ .TableType }};
const fidl_type_t* {{ .Name }}::FidlType = &{{ .TableType }};

void {{ .Name }}::Encode(::overnet::internal::Encoder* _encoder, size_t _offset) {
  {{- range .Members }}
  ::fidl::Encode(_encoder, &{{ .Name }}, _offset + {{ .Offset }});
  {{- end }}
}

void {{ .Name }}::Decode(::overnet::internal::Decoder* _decoder, {{ .Name }}* value, size_t _offset) {
  {{- range .Members }}
  ::fidl::Decode(_decoder, &value->{{ .Name }}, _offset + {{ .Offset }});
  {{- end }}
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

#ifdef FIDL_OPERATOR_EQUALS
bool operator==(const {{ .Name }}& _lhs, const {{ .Name }}& _rhs) {
  {{- range $index, $member := .Members }}
  if (!::fidl::Equals(_lhs.{{ .Name }}, _rhs.{{ .Name }})) {
    return false;
  }
  {{- end }}
  return true;
}
#endif
{{- end }}

{{- define "StructTraits" }}
template <>
struct CodingTraits<{{ .Namespace }}::embedded::{{ .Name }}>
    : public EncodableCodingTraits<{{ .Namespace }}::embedded::{{ .Name }}, {{ .Size }}> {};

inline zx_status_t Clone(const {{ .Namespace }}::embedded::{{ .Name }}& value,
                         {{ .Namespace }}::embedded::{{ .Name }}* result) {
  return {{ .Namespace }}::embedded::Clone(value, result);
}

template <>
struct ToEmbeddedTraits<{{ .Namespace }}::{{ .Name }}> {
  static {{ .Namespace }}::embedded::{{ .Name }} Lift(const {{ .Namespace }}::{{ .Name }}& _value) {
    return {{ .Namespace }}::embedded::{{ .Name }} {
      {{- range .Members }}
      ToEmbedded(_value.{{ .Name }}),
      {{- end }}
    };
  }
};

template<>
struct Equality<{{ .Namespace }}::embedded::{{ .Name }}> {
  static inline bool Equals(const {{ .Namespace }}::embedded::{{ .Name }}& _lhs, const {{ .Namespace }}::embedded::{{ .Name }}& _rhs) {
    {{- range $index, $member := .Members }}
    if (!::fidl::Equals(_lhs.{{ .Name }}, _rhs.{{ .Name }})) {
      return false;
    }
    {{- end }}
    return true;
  }
};
{{- end }}
`
