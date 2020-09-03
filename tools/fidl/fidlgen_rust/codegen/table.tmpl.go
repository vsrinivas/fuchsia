// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const tableTmpl = `
{{- define "TableDeclaration" }}
{{- range .DocComments}}
///{{ . }}
{{- end}}
{{ .Derives }}
pub struct {{ .Name }} {
  {{- range .Members }}
  {{- range .DocComments}}
  ///{{ . }}
  {{- end}}
  pub {{ .Name }}: Option<{{ .Type }}>,
  {{- end }}
}

fidl_table! {
  name: {{ .Name }},
  members: [
    {{- range .Members }}
    {{ .Name }} {
      ty: {{ .Type }},
      ordinal: {{ .Ordinal }},
    },
    {{- end }}
  ],
}
{{- end }}
`
