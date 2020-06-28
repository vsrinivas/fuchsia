// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package lib

import (
	"bytes"
	"compress/gzip"
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
	"strings"
	"text/template"

	"go.fuchsia.dev/fuchsia/tools/check-licenses/templates"
)

func createOutputFile(config *Config) (*os.File, error) {
	path := config.OutputFilePrefix + "." + config.OutputFileExtension
	if shouldCompressOutputFile(config) {
		path = strings.TrimSuffix(path, filepath.Ext(path))
	}
	return os.Create(path)
}

func shouldCompressOutputFile(config *Config) bool {
	return config.OutputFileExtension == "html.gz"
}

func compressOutputFile(config *Config) error {
	path := config.OutputFilePrefix + "." + config.OutputFileExtension
	original_path := strings.TrimSuffix(path, filepath.Ext(path))
	dat, err := ioutil.ReadFile(original_path)
	if err != nil {
		return err
	}
	var buf bytes.Buffer
	zw := gzip.NewWriter(&buf)
	_, err = zw.Write(dat)
	if err != nil {
		return err
	}
	if err := zw.Close(); err != nil {
		return err
	}
	err = ioutil.WriteFile(path, buf.Bytes(), 0644)
	if err != nil {
		return err
	}
	return nil
}

func saveToOutputFile(file *os.File, licenses *Licenses, config *Config, metrics *Metrics) error {
	var unused []*License
	var used []*License
	table_of_contents := make(map[string][]*License)
	for i := range licenses.licenses {
		license := licenses.licenses[i]
		if len(license.matches) == 0 {
			unused = append(unused, license)
		} else {
			used = append(used, license)
			for _, file := range license.matches[0].files {
				table_of_contents[file] = append(table_of_contents[file], license)
			}
		}
	}

	object := struct {
		Used            []*License
		Unused          []*License
		TableOfContents map[string][]*License
	}{
		used,
		unused,
		table_of_contents,
	}
	templateStr := templates.TemplateTxt
	switch config.OutputFileExtension {
	case "txt":
		templateStr = templates.TemplateTxt
	case "html":
		templateStr = templates.TemplateHtml
	case "html.gz":
		templateStr = templates.TemplateHtml
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
		"getCategory": func(license *License) string {
			return strings.TrimSuffix(string((*license).category), ".lic")
		},
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
	if shouldCompressOutputFile(config) {
		compressOutputFile(config)
	}
	return nil
}
