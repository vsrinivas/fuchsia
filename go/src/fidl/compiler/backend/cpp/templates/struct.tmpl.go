// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Struct = `
{{- define "StructDeclaration" -}}
class {{ .Name }}Ptr  {
 public:
  // TODO(TO-747): Generate the C headers and depend on them.
  // using View = {{ .CName }};

  {{- range .Members }}

  const {{ ""|.Type.Decorate }}& {{ .Name }}() const { return {{ .StorageName }}; }
  void set_{{ .Name }}({{ "value"|.Type.Decorate }}) { {{ .StorageName }} = std::move(value); }
  {{- end }}

 private:
  {{- range .Members }}
  {{ .StorageName|.Type.Decorate }};
  {{- end }}
};
{{end}}
`
