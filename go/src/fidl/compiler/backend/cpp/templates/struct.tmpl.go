// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Struct = `
{{- define "StructForwardDeclaration" }}
class {{ .Name }};
{{- end }}

{{- define "StructDeclaration" }}
class {{ .Name }}  {
 public:
  {{ .Name }}();
  {{ .Name }}({{ .Name }}&&);
  {{ .Name }}(const {{ .Name }}&) = delete;
  ~{{ .Name }}();

  {{ .Name }}& operator=({{ .Name }}&&);
  {{ .Name }}& operator=(const {{ .Name }}&) = delete;

  {{- range .Members }}
  const {{ .Type.Decl }}& {{ .Name }}() const { return {{ .StorageName }}; }
  void set_{{ .Name }}({{ .Type.Decl }} value) { {{ .StorageName }} = std::move(value); }
  {{- end }}

  void Encode(::fidl::Encoder* encoder, size_t offset);
  static void Decode(::fidl::Decoder* decoder, {{ .Name }}* value, size_t offset);
  zx_status_t Clone({{ .Name }}* result) const;

 private:
  {{- range .Members }}
  {{ .Type.Decl }} {{ .StorageName }};
  {{- end }}
};
{{- end }}

{{- define "StructDefinition" }}
{{ .Name }}::{{ .Name }}() = default;

{{ .Name }}::{{ .Name }}({{ .Name }}&&) = default;

{{ .Name }}::~{{ .Name }}() = default;

{{ .Name }}& {{ .Name }}::operator=({{ .Name }}&&) = default;

void {{ .Name }}::Encode(::fidl::Encoder* encoder, size_t offset) {
  {{- range .Members }}
  ::fidl::Encode(encoder, &{{ .StorageName }}, offset + {{ .Offset }});
  {{- end }}
}

void {{ .Name }}::Decode(::fidl::Decoder* decoder, {{ .Name }}* value, size_t offset) {
  {{- range .Members }}
  ::fidl::Decode(decoder, &value->{{ .StorageName }}, offset + {{ .Offset }});
  {{- end }}
}

zx_status_t {{ .Name }}::Clone({{ .Name }}* result) const {
  {{- range $index, $member := .Members }}
  {{ if not $index }}zx_status_t {{ end -}}
  status = ::fidl::Clone({{ .StorageName }}, &result->{{ .StorageName }});
  if (status != ZX_OK)
    return status;
  {{- end }}
  return ZX_OK;
}
{{- end }}

{{- define "StructTraits" }}
template <>
struct CodingTraits<{{ .Namespace }}::{{ .Name }}>
    : public EncodableCodingTraits<{{ .Namespace }}::{{ .Name }}, {{ .Size }}> {};

inline zx_status_t Clone(const {{ .Namespace }}::{{ .Name }}& value,
                         {{ .Namespace }}::{{ .Name }}* result) {
return value.Clone(result);
}
{{- end }}
`
