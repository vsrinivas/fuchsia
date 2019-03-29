// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package golang

import (
	"fmt"
	"io"
	"strconv"
	"strings"
	"text/template"

	fidlir "fidl/compiler/backend/types"
	gidlir "gidl/ir"
	gidlmixer "gidl/mixer"
)

var tmpls = template.Must(template.New("tmpls").Parse(`
{{- define "Header"}}
package tbd

import (
	"tbd"
)
{{end -}}

{{- define "SuccessCase"}}
{
{{ .value_build }}
successCase{
	name: {{ .name }},
	input: {{ .value_var }},
	bytes: {{ .bytes }},
}.check(t)
}
{{end -}}
`))

// Generate generates Go tests.
func Generate(wr io.Writer, gidl gidlir.All, fidl fidlir.Root) error {
	if err := tmpls.ExecuteTemplate(wr, "Header", nil); err != nil {
		return err
	}
	if _, err := wr.Write([]byte("func TestAllSuccessCases(t *testing.T) {")); err != nil {
		return err
	}
	for _, success := range gidl.Success {
		decl, err := gidlmixer.ExtractDeclaration(success.Value, fidl)
		if err != nil {
			return fmt.Errorf("success %s: %s", success.Name, err)
		}

		var valueBuilder goValueBuilder
		gidlmixer.Visit(&valueBuilder, success.Value, decl)

		if err := tmpls.ExecuteTemplate(wr, "SuccessCase", map[string]interface{}{
			"name":        strconv.Quote(success.Name),
			"value_build": valueBuilder.String(),
			"value_var":   valueBuilder.lastVar,
			"bytes":       bytesBuilder(success.Bytes),
		}); err != nil {
			return err
		}
	}
	if _, err := wr.Write([]byte("}")); err != nil {
		return err
	}

	return nil
}

func bytesBuilder(bytes []byte) string {
	var builder strings.Builder
	builder.WriteString("[]byte{\n")
	for i, b := range bytes {
		builder.WriteString(fmt.Sprintf("0x%x", b))
		builder.WriteString(",")
		if i%8 == 7 {
			builder.WriteString("\n")
		}
	}
	builder.WriteString("}")
	return builder.String()
}

type goValueBuilder struct {
	strings.Builder
	varidx int

	lastVar string
}

func (b *goValueBuilder) newVar() string {
	b.varidx++
	return fmt.Sprintf("v%d", b.varidx)
}

func (b *goValueBuilder) OnString(value string) {
	stringVar := b.newVar()
	b.Builder.WriteString(stringVar)
	b.Builder.WriteString(":=")
	b.Builder.WriteString(strconv.Quote(value))
	b.Builder.WriteString("\n")
	b.lastVar = stringVar
}

func (b *goValueBuilder) OnStruct(value gidlir.Object, decl *gidlmixer.StructDecl) {
	structVar := b.newVar()
	b.Builder.WriteString(structVar)
	b.Builder.WriteString(":=")
	b.Builder.WriteString(value.Name)
	b.Builder.WriteString("{}")
	b.Builder.WriteString("\n")
	for key, field := range value.Fields {
		fieldDecl, _ := decl.ForKey(key)
		gidlmixer.Visit(b, field, fieldDecl)
		fieldVar := b.lastVar
		b.Builder.WriteString(structVar)
		b.Builder.WriteString(".")
		b.Builder.WriteString(key)
		b.Builder.WriteString("=")
		b.Builder.WriteString(fieldVar)
		b.Builder.WriteString("\n")
	}
	b.lastVar = structVar
}
