// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Enum = `
{{- define "EnumForwardDeclaration" }}
enum class {{ .Name }} : {{ .Type }} {
  {{- range .Members }}
  {{ .Name }} = {{ .Value }},
  {{- end }}
};

inline zx_status_t Clone({{ .Namespace }}::embedded::{{ .Name }} value,
                         {{ .Namespace }}::embedded::{{ .Name }}* result) {
  *result = value;
  return ZX_OK;
}
{{ end }}

{{- define "EnumTraits" }}
template <>
struct CodingTraits<{{ .Namespace }}::embedded::{{ .Name }}> {
  static constexpr size_t encoded_size = sizeof({{ .Namespace }}::embedded::{{ .Name }});
  static void Encode(::overnet::internal::Encoder* encoder, {{ .Namespace }}::embedded::{{ .Name }}* value, size_t offset) {
    {{ .Type }} underlying = static_cast<{{ .Type }}>(*value);
    ::fidl::Encode(encoder, &underlying, offset);
  }
  static void Decode(::overnet::internal::Decoder* decoder, {{ .Namespace }}::embedded::{{ .Name }}* value, size_t offset) {
    {{ .Type }} underlying = {};
    ::fidl::Decode(decoder, &underlying, offset);
    *value = static_cast<{{ .Namespace }}::embedded::{{ .Name }}>(underlying);
  }
};

inline zx_status_t Clone({{ .Namespace }}::embedded::{{ .Name }} value,
                         {{ .Namespace }}::embedded::{{ .Name }}* result) {
  return {{ .Namespace }}::embedded::Clone(value, result);
}
{{- end }}
`
