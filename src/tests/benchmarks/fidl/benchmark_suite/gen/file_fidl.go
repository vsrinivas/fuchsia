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

var fidlTmpl = template.Must(template.New("fidlTmpl").Parse(
	`// Copyright {{ .Year }} The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

library benchmarkfidl;

{{ .Definitions }}
`))

func writeFidl(w io.Writer, definitions []string) error {
	allDefs := strings.ReplaceAll(
		strings.Join(definitions, "\n\n"),
		"\t",
		"    ")
	return fidlTmpl.Execute(w, map[string]interface{}{
		"Year":        time.Now().Year(),
		"Definitions": allDefs,
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
