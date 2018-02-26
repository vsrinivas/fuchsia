// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Union = `
{{- define "UnionForwardDeclaration" }}
class {{ .Name }};
{{- end }}

{{- define "UnionDeclaration" }}
class {{ .Name }} {
 public:
  {{ .Name }}();
  {{ .Name }}({{ .Name }}&&);
  ~{{ .Name }}();

  ::fidl_union_tag_t tag;
  union {
    {{- range .Members }}
    {{ .Type.Decl }} {{ .Name }};
    {{- end }}
  };
};
{{- end }}

{{- define "UnionDefinition" }}
{{ .Name }}::{{ .Name }}() {}
{{ .Name }}::{{ .Name }}({{ .Name }}&&) {}
{{ .Name }}::~{{ .Name }}() {}
{{- end }}
`
