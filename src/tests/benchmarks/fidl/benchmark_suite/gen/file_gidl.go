// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"io"
	"log"
	"os"
	"path"
	"strings"
	"text/template"
	"time"
)

var gidlTmpl = template.Must(template.New("gidlTmpl").Parse(
	`// Copyright {{ .Year }} The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

{{- range .Benchmarks }}

benchmark("{{ .Name }}") {
    value = {{ .Value }},
    {{- if .Allowlist }}
    bindings_allowlist = {{ .Allowlist }},
    {{- end -}}
    {{- if .Denylist }}
    bindings_denylist = {{ .Denylist }},
    {{- end }}
}
{{- end }}
`))

type gidlResult struct {
	Name                string
	Value               string
	Allowlist, Denylist string
}

func writeGidl(w io.Writer, results []gidlResult) error {
	return gidlTmpl.Execute(w, map[string]interface{}{
		"Year":       time.Now().Year(),
		"Benchmarks": results,
	})
}

func formatBindingList(bindings []Binding) string {
	if len(bindings) == 0 {
		return ""
	}
	strs := make([]string, len(bindings))
	for i, binding := range bindings {
		strs[i] = string(binding)
	}
	return "[" + strings.Join(strs, ", ") + "]"
}

// Strip out existing indentation and use count of open braces to place new indentation.
func fixValueIndentation(value string) string {
	indentationMark := "    "
	nIndent := 1
	var builder strings.Builder

	runes := []rune(strings.Trim(value, " \t\n"))
	for i := 0; i < len(runes); i++ {
		r := runes[i]
		builder.WriteRune(r)
		switch r {
		case '{', '[':
			nIndent++
		case '}', ']':
			nIndent--
		case '\n':
		loop:
			for i+1 < len(runes) {
				switch runes[i+1] {
				case ' ', '\t':
					i++
				default:
					break loop
				}
			}
			effectiveIndent := nIndent
			if i+1 < len(runes) && (runes[i+1] == '}' || runes[i+1] == ']') {
				effectiveIndent--
			}
			if i+1 == len(runes) || runes[i+1] != '\n' {
				builder.WriteString(strings.Repeat(indentationMark, effectiveIndent))
			}
		}
	}
	return builder.String()
}

func genGidlFile(filepath string, gidl GidlFile) error {
	var results []gidlResult
	for _, benchmark := range gidl.Benchmarks {
		value, err := gidl.Gen(benchmark.Config)
		if err != nil {
			return err
		}
		results = append(results, gidlResult{
			Name:      benchmark.Name,
			Value:     fixValueIndentation(value),
			Allowlist: formatBindingList(benchmark.Allowlist),
			Denylist:  formatBindingList(benchmark.Denylist),
		})
	}

	f, err := os.Create(filepath)
	if err != nil {
		return err
	}
	defer f.Close()
	if err := writeGidl(f, results); err != nil {
		return err
	}
	return nil
}

func genGidl(outdir string) {
	for _, gidl := range allGidlFiles() {
		filepath := path.Join(outdir, gidl.Filename)
		if err := genGidlFile(filepath, gidl); err != nil {
			log.Fatalf("Error generating %s: %s", gidl.Filename, err)
		}
	}
}
