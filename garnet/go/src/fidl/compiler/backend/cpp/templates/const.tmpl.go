// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Const = `
{{- define "ConstDeclaration" }}
{{range .DocComments}}
///{{ . }}
{{- end}}
{{- if .Extern }}
extern {{ .Decorator }} {{ .Type.Decl }} {{ .Name }};
{{- else }}
{{ .Decorator }} {{ .Type.Decl }} {{ .Name }} = {{ .Value }};
{{- end }}
{{- end }}

{{- define "ConstDefinition" }}
{{- if .Extern }}
{{ .Decorator }} {{ .Type.Decl }} {{ .Name }} = {{ .Value }};
{{- end }}
{{- end }}
`
