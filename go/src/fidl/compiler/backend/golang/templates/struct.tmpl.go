// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Struct = `
{{- define "StructDefinition" -}}
type {{ .Name }} struct {
	{{- range .Members }}
	{{ .Name }} {{ .Type }}
	{{- end }}
}

// Implements Payload.
func (_ *{{ .Name }}) InlineAlignment() int {
	return {{ .Alignment }}
}

// Implements Payload.
func (_ *{{ .Name }}) InlineSize() int {
	return {{ .Size }}
}
{{- end -}}
`
