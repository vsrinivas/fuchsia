// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Union = `
{{- define "UnionDeclaration" }}
{{- if .HasAttribute "Result" }}
type {{ .Name }} = std::result::Result< {{(index .Members 0).Type }} , {{ (index .Members 1).Type }} >;
{{ else }}
fidl_union! {
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
{{- end }}
{{- end }}
`
