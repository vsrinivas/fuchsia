// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const enumTemplate = `
{{- define "EnumForwardDeclaration" }}
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

inline zx_status_t Clone({{ .Namespace }}::{{ .Name }} value,
                         {{ .Namespace }}::{{ .Name }}* result) {
  *result = value;
  return ZX_OK;
}
{{ end }}

{{- define "EnumTraits" }}
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
  bool operator()(const {{ .Namespace }}::{{ .Name }}& _lhs, const {{ .Namespace }}::{{ .Name }}& _rhs) const {
    return _lhs == _rhs;
  }

  static inline bool Equals(const {{ .Namespace }}::{{ .Name }}& _lhs, const {{ .Namespace }}::{{ .Name }}& _rhs) {
    // TODO(46638): Remove this when all clients have been transitioned to functor.
    return ::fidl::Equality<{{ .Namespace }}::{{ .Name }}>{}(_lhs, _rhs);
  }
};

{{ end }}
`
