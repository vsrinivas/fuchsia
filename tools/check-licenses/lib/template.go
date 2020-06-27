// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package lib

import (
	"fmt"
	"os"
	"strings"
	"text/template"

	"go.fuchsia.dev/fuchsia/tools/check-licenses/templates"
)

func createOutputFile(config *Config) (*os.File, error) {
	return os.Create(config.OutputFilePrefix + "." + config.OutputFileExtension)
}

func saveToOutputFile(file *os.File, licenses *Licenses, config *Config, metrics *Metrics) error {
	// TODO(solomonkinard) save path of single license files if license found there too
	var unused []*License
	var used []*License
	for i := range licenses.licenses {
		license := licenses.licenses[i]
		if len(license.matches) == 0 {
			unused = append(unused, license)
		} else {
			used = append(used, license)
		}
	}

	object := struct {
		Signature string
		Used      []*License
		Unused    []*License
	}{
		// TODO(solomonkinard) signature can be checksum of generated file
		"SiGnAtUrE",
		used,
		unused,
	}
	should_gz_compress := false
	templateStr := templates.TemplateTxt
	switch config.OutputFileExtension {
	case "txt":
		templateStr = templates.TemplateTxt
	case "html":
		templateStr = templates.TemplateHtml
	case "html.gz":
		templateStr = templates.TemplateHtml
		should_gz_compress = true
	case "json":
		templateStr = templates.TemplateJson
	default:
		fmt.Println("error: no template found")
	}
	tmpl := template.Must(template.New("name").Funcs(template.FuncMap{
		"getPattern": func(license *License) string { return (*license).pattern.String() },
		"getText":    func(license *License) string { return string((*license).matches[0].value) },
		"getHTMLText": func(license *License) string {
			return strings.Replace(string((*license).matches[0].value), "\n", "<br />", -1)
		},
		"getEscapedText": func(license *License) string {
			return strings.Replace(string((*license).matches[0].value), "\"", "\\\"", -1)
		},
		"getCategory": func(license *License) string { return string((*license).category) },
		"getFiles": func(license *License) *[]string {
			var files []string
			for _, match := range license.matches {
				for _, file := range match.files {
					files = append(files, file)
				}
			}
			return &files
		},
	}).Parse(templateStr))
	if err := tmpl.Execute(file, object); err != nil {
		return err
	}
	if should_gz_compress {
		// TODO(solomonkinard) gzip compress file
	}
	return nil
}
