// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bytes"
	"encoding/json"
	"go/format"
	"io/ioutil"
	"log"
	"os"
	"text/template"
)

type fifoTemplateArgs struct {
	Pkg       string
	Handler   string
	Entries   string
	EntryType string
	Signal    bool
	Imports   []string
}

func main() {
	log.SetFlags(log.Lshortfile)

	args := os.Args[1:]
	if len(args) < 3 {
		log.Fatalf("Invalid number of arguments, expected 3: [template path] [destination] [template values JSON]")
	}
	templateFilePath := args[0]
	destinationPath := args[1]
	jsonTemplatePath := args[2]

	tmpl, err := template.ParseFiles(templateFilePath)
	if err != nil {
		log.Fatalf("Failed to parse template: %s", err)
	}

	jsonFile, err := ioutil.ReadFile(jsonTemplatePath)
	if err != nil {
		log.Fatalf("Failed to read JSON file at %s: %s", jsonTemplatePath, err)
	}

	var jsonContent fifoTemplateArgs
	decoder := json.NewDecoder(bytes.NewReader(jsonFile))
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(&jsonContent); err != nil {
		log.Fatalf("Failed to parse JSON contents at %s: %s", jsonTemplatePath, err)
	}

	var b bytes.Buffer
	if err := tmpl.Execute(&b, jsonContent); err != nil {
		log.Fatalf("Failed to execute template: %s", err)
	}

	src, err := format.Source(b.Bytes())
	if err != nil {
		log.Fatalf("Failed to format source: %s%s", err, b.String())
	}

	if err := ioutil.WriteFile(destinationPath, src, 0644); err != nil {
		log.Fatalf("Failed to write destination file: %s: %s", destinationPath, err)
	}
}
