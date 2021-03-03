// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentConstTmpl = `
{{- define "ConstDeclaration" }}
{{ EnsureNamespace . }}
{{ range .DocComments }}
//{{ . }}
{{- end }}
{{- if .Extern }}
extern {{ .Decorator }} {{ .Type }} {{ .Name }};
{{- EnsureNamespace .WireAlias }}
extern {{ .Decorator }} {{ .Type }} {{ .WireAlias.Name }};
{{- else }}
{{ .Decorator }} {{ .Type }} {{ .Name }} = {{ .Value }};
{{- EnsureNamespace .WireAlias }}
{{ .Decorator }} {{ .Type }} {{ .WireAlias.Name }} = {{ .Value }};
{{- end }}
{{- end }}

{{- define "ConstDefinition" }}
{{- if .Extern }}
{{ EnsureNamespace "::" }}
{{ .Decorator }} {{ .Type }} {{ . }} = {{ .Value }};
{{ .Decorator }} {{ .Type }} {{ .WireAlias }} = {{ .Value }};
{{- end }}
{{- end }}
`
