// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Struct = `
{{- define "StructDeclaration" }}

{{- if .Members }}

{{- range .DocComments}}
///{{ . }}
{{- end}}
{{ .Derives }}
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
      offset_v1: {{ .OffsetV1 }},
    },
  {{- end }}
  ],
  size_v1: {{ .SizeV1 }},
  align_v1: {{ .AlignmentV1 }},
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
