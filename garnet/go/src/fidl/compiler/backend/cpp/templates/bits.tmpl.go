// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Bits = `
{{- define "BitsForwardDeclaration" }}
{{range .DocComments}}
///{{ . }}
{{- end}}
enum class {{ .Name }} : {{ .Type }} {
  {{- range .Members }}
  {{range .DocComments}}
  ///{{ . }}
  {{- end}}
  {{ .Name }} = {{ .Value }},
  {{- end }}
};

const static {{ .Name }} {{ .MaskName }} = static_cast<{{ .Name }}>({{ .Mask }}u);

inline zx_status_t Clone({{ .Namespace }}::{{ .Name }} value,
                         {{ .Namespace }}::{{ .Name }}* result) {
  *result = value;
  return ZX_OK;
}

constexpr inline {{ .Namespace }}::{{ .Name }} operator|({{ .Namespace }}::{{ .Name }} _lhs,
                                                         {{ .Namespace }}::{{ .Name }} _rhs) {
  return static_cast<{{ .Namespace }}::{{ .Name }}>(
    static_cast<{{ .Type }}>(_lhs) | static_cast<{{ .Type }}>(_rhs));
}

constexpr inline {{ .Namespace }}::{{ .Name }}& operator|=({{ .Namespace }}::{{ .Name }}& _lhs,
                                                           {{ .Namespace }}::{{ .Name }} _rhs) {
  _lhs = _lhs | _rhs;
  return _lhs;
}

constexpr inline {{ .Namespace }}::{{ .Name }} operator&({{ .Namespace }}::{{ .Name }} _lhs,
                                                         {{ .Namespace }}::{{ .Name }} _rhs) {
  return static_cast<{{ .Namespace }}::{{ .Name }}>(
    static_cast<{{ .Type }}>(_lhs) & static_cast<{{ .Type }}>(_rhs));
}

constexpr inline {{ .Namespace }}::{{ .Name }}& operator&=({{ .Namespace }}::{{ .Name }}& _lhs,
                                                           {{ .Namespace }}::{{ .Name }} _rhs) {
  _lhs = _lhs & _rhs;
  return _lhs;
}

constexpr inline {{ .Namespace }}::{{ .Name }} operator^({{ .Namespace }}::{{ .Name }} _lhs,
                                                         {{ .Namespace }}::{{ .Name }} _rhs) {
  return static_cast<{{ .Namespace }}::{{ .Name }}>(
    static_cast<{{ .Type }}>(_lhs) ^ static_cast<{{ .Type }}>(_rhs));
}

constexpr inline {{ .Namespace }}::{{ .Name }}& operator^=({{ .Namespace }}::{{ .Name }}& _lhs,
                                                           {{ .Namespace }}::{{ .Name }} _rhs) {
  _lhs = _lhs ^ _rhs;
  return _lhs;
}

constexpr inline {{ .Namespace }}::{{ .Name }} operator~({{ .Namespace }}::{{ .Name }} _value) {
  return static_cast<{{ .Namespace }}::{{ .Name }}>(
    ~static_cast<{{ .Type }}>(_value) & static_cast<{{ .Type }}>({{ .MaskName }}));
}
{{ end }}

{{- define "BitsTraits" }}
template <>
struct CodingTraits<{{ .Namespace }}::{{ .Name }}> {
  static constexpr size_t inline_size_old = sizeof({{ .Namespace }}::{{ .Name }});
  static constexpr size_t inline_size_v1_no_ee = sizeof({{ .Namespace }}::{{ .Name }});
  static void Encode(Encoder* encoder, {{ .Namespace }}::{{ .Name }}* value, size_t offset) {
    {{ .Type }} underlying = static_cast<{{ .Type }}>(*value);
    ::fidl::Encode(encoder, &underlying, offset);
  }
  static void Decode(Decoder* decoder, {{ .Namespace }}::{{ .Name }}* value, size_t offset) {
    {{ .Type }} underlying = {};
    ::fidl::Decode(decoder, &underlying, offset);
    *value = static_cast<{{ .Namespace }}::{{ .Name }}>(underlying);
  }
};

inline zx_status_t Clone({{ .Namespace }}::{{ .Name }} value,
                         {{ .Namespace }}::{{ .Name }}* result) {
  return {{ .Namespace }}::Clone(value, result);
}

template<>
struct Equality<{{ .Namespace }}::{{ .Name }}> {
  static inline bool Equals(const {{ .Namespace }}::{{ .Name }}& _lhs, const {{ .Namespace }}::{{ .Name }}& _rhs) {
    {{ .Type }} _lhs_underlying = static_cast<{{ .Type }}>(_lhs);
    {{ .Type }} _rhs_underlying = static_cast<{{ .Type }}>(_rhs);
    return Equality<{{ .Type }}>::Equals(_lhs_underlying, _rhs_underlying);
  }
};
{{- end }}
`
