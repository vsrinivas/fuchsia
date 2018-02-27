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
{{- end }}

{{- define "StructTraits" }}
template <>
struct CodingTraits<{{ .Namespace }}::{{ .Name }}>
    : public EncodableCodingTraits<{{ .Namespace }}::{{ .Name }}, {{ .Size }}> {};
{{- end }}
`
