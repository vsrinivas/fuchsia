// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"os"
	"path/filepath"
	"text/template"
)

type TestFidlGenerator struct{}

const ownerReadWriteNoExecute = 0644

const sourceFileTemplate = `
{{- define "GenerateSourceFile" -}}
	{{ range $const := .ConstDeclarations }}
{{ template "ConstDeclaration" $const }}
	{{ end }}
	{{ range $enum := .EnumDeclarations }}
{{ template "EnumDeclaration" $enum }}
	{{ end }}
	{{ range $interface := .InterfaceDeclarations }}
{{ template "InterfaceDeclaration" $interface }}
	{{ end }}
	{{ range $moddep := .ModuleDependencies }}
{{ template "ModuleDependency" $moddep }}
	{{ end }}
	{{ range $struct := .StructDeclarations }}
{{ template "StructDeclaration" $struct }}
	{{ end }}
	{{ range $union := .UnionDeclarations }}
{{ template "UnionDeclaration" $union }}
	{{ end }}
{{- end -}}
`
const literalTemplate = `
{{- define "Literal" -}}
	{{- if .IsString -}}
		"{{- .String -}}"
	{{- else if .IsNumeric -}}
		{{ .Numeric }}
	{{- else if .IsBoolean -}}
		{{ .Boolean }}
	{{- else if .IsDefault -}}
		default
	{{- else -}}
		missingliteral
	{{- end }}
{{- end -}}
`

const constDeclTemplate = `
{{- define "Constant" -}}
	{{- if .IsLiteral -}}
		{{- template "Literal" .LiteralValue -}}
	{{- else if .IsIdentifier }}
		{{- .Identifier -}}
	{{- end -}}
{{- end -}}

{{- define "ConstDeclaration" -}}
const {{.Name}}: {{.Type}} = {{ template "Constant" .Value }};
{{- end -}}
`

const enumDeclTemplate = `
{{- define "EnumDeclaration" -}}
enum {{ .Name }}: {{ .UnderlyingType }} {
  {{ range $value := .Values }}
  {{- $value.Name }} = {{ template "Constant" .Value }},
  {{ end }}
}
{{end}}
`

const interfaceDeclTemplate = `
{{- define "Params" -}}
  {{- range $param := . -}}{{ $param.Name }}: {{ $param.Type }},{{- end -}}
{{ end }}

{{- define "InterfaceDeclaration" -}}
interface {{ .Name }} {
  {{ range $method := .Methods }}
  fn {{ $method.Ordinal }}: {{ $method.Name -}}
  ({{ template "Params" .Request }}) -> ({{ template "Params" .Response }})
  {{ end }}
}
{{end}}
`

const structDeclTemplate = `
{{- define "StructDeclaration" -}}
struct {{ .Name }} { # size {{ .Size }}
  {{ range $field := .Fields }}
  {{ $field.Name }}: {{ $field.Type }} # offset {{ $field.Offset }}
  {{ end }}
}
{{end}}
`

const unionDeclTemplate = `
{{- define "UnionDeclaration" -}}
union {{ .Name }} { # size {{ .Size }}
  {{ range $field := .Fields }}
  {{ $field.Name }}: {{ $field.Type }}
  {{ end }}
}
{{end}}
`

const moduleDepTemplate = `
{{- define "ModuleDependency" -}}
{{end}}
`

func (_ TestFidlGenerator) GenerateFidl(
	fidlData Root, _args []string,
	outputDir string, srcRootPath string) error {
	parentDir := filepath.Join(outputDir, srcRootPath)
	err := os.MkdirAll(parentDir, ownerReadWriteNoExecute)
	if err != nil {
		return err
	}

	outputFilename := filepath.Join(parentDir, "generated.test")
	f, err := os.Create(outputFilename)
	if err != nil {
		return err
	}
	defer f.Close()

	tmpls := template.New("TestTemplates")
	template.Must(tmpls.Parse(sourceFileTemplate))
	template.Must(tmpls.Parse(literalTemplate))
	template.Must(tmpls.Parse(constDeclTemplate))
	template.Must(tmpls.Parse(enumDeclTemplate))
	template.Must(tmpls.Parse(interfaceDeclTemplate))
	template.Must(tmpls.Parse(structDeclTemplate))
	template.Must(tmpls.Parse(unionDeclTemplate))
	template.Must(tmpls.Parse(moduleDepTemplate))
	return tmpls.ExecuteTemplate(f, "GenerateSourceFile", fidlData)
}
