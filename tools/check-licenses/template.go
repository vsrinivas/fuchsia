// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package checklicenses

import (
	"bytes"
	"compress/gzip"
	"fmt"
	"io/ioutil"
	"sort"
	"strings"
	"text/template"

	"go.fuchsia.dev/fuchsia/tools/check-licenses/templates"
)

// saveToOutputFile writes to output the serialized licenses.
//
// It writes an uncompressed version too if a compressed version is requested.
func saveToOutputFile(path string, licenses *Licenses) error {
	// Sort the licenses in alphabetical order for consistency.
	sort.Slice(licenses.licenses, func(i, j int) bool { return licenses.licenses[i].category < licenses.licenses[j].category })
	data := struct {
		Used   []*License
		Unused []*License
	}{}
	for _, l := range licenses.licenses {
		if len(l.matches) == 0 {
			data.Unused = append(data.Unused, l)
		} else {
			data.Used = append(data.Used, l)
		}
	}

	templateStr := ""
	switch {
	case strings.HasSuffix(path, ".txt"):
		templateStr = templates.TemplateTxt
	case strings.HasSuffix(path, ".html") || strings.HasSuffix(path, ".html.gz"):
		// TODO(omerlevran): Use html/template instead of text/template.
		// text/template is inherently unsafe to generate HTML.
		templateStr = templates.TemplateHtml
	case strings.HasSuffix(path, ".json"):
		// TODO(omerlevran): Use encoding/json instead of hand-rolling out json.
		templateStr = templates.TemplateJson
	default:
		return fmt.Errorf("no template found for %s", path)
	}
	buf := bytes.Buffer{}
	tmpl := template.Must(template.New("name").Funcs(funcMap).Parse(templateStr))
	if err := tmpl.Execute(&buf, data); err != nil {
		return err
	}

	// Special handling for compressed file.
	const gz = ".gz"
	if strings.HasSuffix(path, gz) {
		// First write uncompressed, then compressed.
		if err := ioutil.WriteFile(path[:len(path)-len(gz)], buf.Bytes(), 0666); err != nil {
			return err
		}
		d, err := compressGZ(buf.Bytes())
		if err != nil {
			return err
		}
		return ioutil.WriteFile(path, d, 0666)
	}

	return ioutil.WriteFile(path, buf.Bytes(), 0666)
}

// compressGZ returns the compressed buffer with gzip format.
func compressGZ(d []byte) ([]byte, error) {
	buf := bytes.Buffer{}
	zw := gzip.NewWriter(&buf)
	if _, err := zw.Write(d); err != nil {
		return nil, err
	}
	if err := zw.Close(); err != nil {
		return nil, err
	}
	return buf.Bytes(), nil
}

var funcMap = template.FuncMap{
	"getPattern": func(l *License) string {
		return l.pattern.String()
	},
	"getText": func(l *License, author string) string {
		return l.matches[author].value
	},
	"getHTMLText": func(l *License, author string) string {
		return strings.Replace(l.matches[author].value, "\n", "<br />", -1)
	},
	"getEscapedText": func(l *License, author string) string {
		return strings.Replace(l.matches[author].value, "\"", "\\\"", -1)
	},
	"getCategory": func(l *License) string {
		return strings.TrimSuffix(l.category, ".lic")
	},
	"getFiles": func(l *License, author string) []string {
		var files []string
		for _, file := range l.matches[author].files {
			files = append(files, file)
		}
		sort.Strings(files)
		return files
	},
	"getAuthors": func(l *License) []string {
		var authors []string
		for author := range l.matches {
			authors = append(authors, author)
		}
		sort.Strings(authors)
		return authors
	},
}
