// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"io"
	"log"
	"os"
	"path"
	"text/template"
	"time"
)

var fidlTmpl = template.Must(template.New("fidlTmpl").Parse(
	`// Copyright {{ .Year }} The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// GENERATED FILE: Regen with out/default/host-tools/regen_fidl_benchmark_suite

library benchmarkfidl;

{{ range .Definitions }}
{{ . }}
{{ end -}}
`))

func writeFidl(w io.Writer, definitions []string) error {
	var formattedDefs []string
	for _, def := range definitions {
		formattedDefs = append(formattedDefs, format(0, def))
	}
	return fidlTmpl.Execute(w, map[string]interface{}{
		"Year":        time.Now().Year(),
		"Definitions": formattedDefs,
	})
}

func genFidlFile(filepath string, fidl FidlFile) error {
	var definitions []string
	for _, definition := range fidl.Definitions {
		out, err := fidl.Gen(definition.Config)
		if err != nil {
			return err
		}
		definitions = append(definitions, out)
	}

	f, err := os.Create(filepath)
	if err != nil {
		return err
	}
	defer f.Close()
	if err := writeFidl(f, definitions); err != nil {
		return err
	}
	return nil
}

func genFidl(outdir string) {
	for _, fidl := range allFidlFiles() {
		filepath := path.Join(outdir, fidl.Filename)
		if err := genFidlFile(filepath, fidl); err != nil {
			log.Fatalf("Error generating %s: %s", fidl.Filename, err)
		}
	}
}
