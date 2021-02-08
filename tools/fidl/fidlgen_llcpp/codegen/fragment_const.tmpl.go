// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentConstTmpl = `
{{- define "ConstDeclaration" }}
{{ EnsureNamespace .Decl.Wire }}
{{range .DocComments}}
//{{ . }}
{{- end}}
{{- if .Extern }}
extern {{ .Decorator }} {{ .Type.Wire }} {{ .Decl.Wire.Name }};
{{- else }}
{{ .Decorator }} {{ .Type.Wire }} {{ .Decl.Wire.Name }} = {{ .Value.Wire }};
{{- end }}
{{- end }}

{{- define "ConstDefinition" }}
{{- if .Extern }}
{{ EnsureNamespace "::" }}
{{ .Decorator }} {{ .Type.Wire }} {{ .Decl.Wire }} = {{ .Value.Wire }};
{{- end }}
{{- end }}
`
