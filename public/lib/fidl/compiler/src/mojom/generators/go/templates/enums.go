// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

import (
	"text/template"
)

const enumDeclTmplText = `
{{- define "EnumDecl" -}}
{{$enum := . -}}
type {{$enum.Name}} int32

{{ template "RuntimeTypeAccessors" $enum }}

const (
{{- range $value := $enum.Values -}}
	{{$value.Name}} {{$enum.Name}} = {{$value.Value}}
{{end}}
)

{{- end -}}
`

func initEnumTemplates() {
	template.Must(goFileTmpl.Parse(enumDeclTmplText))
}
