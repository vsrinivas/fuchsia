// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentConstTmpl = `
{{- define "ConstDeclaration" }}
{{range .DocComments}}
//{{ . }}
{{- end}}
{{- if .Extern }}
extern {{ .Decorator }} {{ .Type.LLDecl }} {{ .Name }};
{{- else }}
{{ .Decorator }} {{ .Type.LLDecl }} {{ .Name }} = {{ .Value }};
{{- end }}
{{- end }}

{{- define "ConstDefinition" }}
{{- if .Extern }}
{{ .Decorator }} {{ .Type.LLDecl }} {{ .Name }} = {{ .Value }};
{{- end }}
{{- end }}
`
