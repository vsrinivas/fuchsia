// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

import (
	"bytes"
	"io/ioutil"
	"text/template"

	"mojom/generators/go/gofmt"
	"mojom/generators/go/translator"
)

// goFileTmpl is the template object for a go file.
var goFileTmpl *template.Template

// ExecuteTemplates accepts a translator.TmplFile and returns the formatted
// source code for the go bindings.
func ExecuteTemplates(tmplFile *translator.TmplFile, funcMap template.FuncMap) string {
	buffer := &bytes.Buffer{}
	goFileTmpl.Funcs(funcMap)
	if err := goFileTmpl.ExecuteTemplate(buffer, "FileTemplate", tmplFile); err != nil {
		panic(err)
	}

	unformatted := buffer.String()
	src, err := gofmt.FormatGoFile(unformatted)

	if err != nil {
		ioutil.WriteFile("/tmp/unformatted.go", buffer.Bytes(), 0666)
		panic(err)
	}
	return src
}

func init() {
	// We parse the subtemplates only once.
	goFileTmpl = template.New("GoFileTemplate")

	goFileTmpl.Funcs(template.FuncMap{
		"TypesPkg":    func() string { return "mojom_types." },
		"DescPkg":     func() string { return "service_describer." },
		"GenTypeInfo": func() bool { return false },
		"IsUnionField": func(templateType interface{}) bool {
			_, ok := templateType.(translator.UnionFieldTemplate)
			return ok
		},
		"IsInterface": func(templateType interface{}) bool {
			_, ok := templateType.(translator.InterfaceTemplate)
			return ok
		},
	})

	template.Must(goFileTmpl.Parse(goFileTemplate))

	initEncodingTemplates()
	initDecodingTemplates()
	initStructTemplates()
	initUnionTemplates()
	initEnumTemplates()
	initInterfaceTemplates()
	initRuntimeTypeInfoTemplates()
}

const goFileTemplate = `
{{- define "FileTemplate" -}}
{{- $fileTmpl := . -}}
package {{$fileTmpl.PackageName}}

import (
	{{range $import := $fileTmpl.Imports}}
	{{$import.PackageName}} "{{$import.PackagePath}}"
	{{- end}}
)

{{ template "GetRuntimeTypeInfo" $fileTmpl }}

{{- range $struct := $fileTmpl.Structs}}
	{{ template "Struct" $struct }}
{{- end}}

{{- range $union := $fileTmpl.Unions}}
	{{ template "Union" $union }}
{{- end}}

{{- range $enum := $fileTmpl.Enums}}
	{{ template "EnumDecl" $enum }}
{{- end}}

{{- range $interface := $fileTmpl.Interfaces}}
	{{ template "Interface" $interface }}
{{- end}}

{{- range $constant := $fileTmpl.Constants}}
const {{$constant.Name}} {{$constant.Type}} = {{$constant.Value}}
{{end}}
{{- end -}}
`
