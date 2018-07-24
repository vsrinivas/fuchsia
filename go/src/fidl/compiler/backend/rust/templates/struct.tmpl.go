// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Struct = `
{{- define "StructDeclaration" }}
{{- if not .LargeArrays }}
#[derive(Debug, PartialEq)]
{{- end }}
pub struct {{ .Name }} {
  {{- range .Members }}
  pub {{ .Name }}: {{ .Type }},
  {{- end }}
}

fidl2_struct! {
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
`
