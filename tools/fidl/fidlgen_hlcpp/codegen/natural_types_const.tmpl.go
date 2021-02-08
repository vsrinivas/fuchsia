// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const constTemplate = `
{{- define "ConstDeclaration" }}
{{ EnsureNamespace .Decl.Natural }}
{{range .DocComments}}
///{{ . }}
{{- end}}
{{- if .Extern }}
extern {{ .Decorator }} {{ .Type.Natural }} {{ .Decl.Natural.Name }};
{{- else }}
{{ .Decorator }} {{ .Type.Natural }} {{ .Decl.Natural.Name }} = {{ .Value.Natural }};
{{- end }}
{{- end }}

{{- define "ConstDefinition" }}
{{- if .Extern }}
{{ EnsureNamespace .Decl.Natural }}
{{ .Decorator }} {{ .Type.Natural }} {{ .Decl.Natural.Name }} = {{ .Value.Natural }};
{{- end }}
{{- end }}
`
