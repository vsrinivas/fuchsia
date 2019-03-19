// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Struct = `
{{- define "StructDeclaration" }}

{{- if .Members }}

{{- if not .LargeArrays }}
#[derive(Debug, PartialEq)]
{{- end }}
{{- range .DocComments}}
///{{ . }}
{{- end}}
pub struct {{ .Name }} {
  {{- range .Members }}
  {{- range .DocComments}}
  ///{{ . }}
  {{- end}}
  pub {{ .Name }}: {{ .Type }},
  {{- end }}
}

fidl_struct! {
  name: {{ .Name }},
  members: [
  {{- range .Members }}
    {{ .Name }} {
      ty: {{ .Type }},
      offset: {{ .Offset }},
    },
  {{- end }}
  ],
  size: {{ .Size }},
  align: {{ .Alignment }},
}
{{- else }}

fidl_empty_struct!(
	{{- range .DocComments}}
	///{{ . }}
	{{- end}}
	{{ .Name }}
);

{{- end }}
{{- end }}
`
