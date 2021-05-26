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
func saveToOutputFile(path string, licenses *Licenses, config *Config) error {
	// Sort the licenses in alphabetical order for consistency.
	sort.Slice(licenses.licenses, func(i, j int) bool { return licenses.licenses[i].Category < licenses.licenses[j].Category })
	data := struct {
		Used   []*License
		Unused []*License
	}{}

	for _, l := range licenses.licenses {
		isUsed := false
		for _, m := range l.matches {
			if m.Used {
				isUsed = true
			}
			break
		}
		if isUsed {
			data.Used = append(data.Used, l)
		} else {
			data.Unused = append(data.Unused, l)
		}
	}
	for _, n := range licenses.notices {
		data.Used = append(data.Used, n)
	}

	templateStr := ""
	switch {
	case strings.HasSuffix(path, ".txt") || strings.HasSuffix(path, ".txt.gz"):
		templateStr = templates.TemplateTxt
	case strings.HasSuffix(path, ".html") || strings.HasSuffix(path, ".html.gz"):
		// TODO(omerlevran): Use html/template instead of text/template.
		// text/template is inherently unsafe to generate HTML.
		templateStr = templates.TemplateHtml
	case strings.HasSuffix(path, ".json") || strings.HasSuffix(path, ".json.gz"):
		// TODO(omerlevran): Use encoding/json instead of hand-rolling out json.
		templateStr = templates.TemplateJson
	default:
		return fmt.Errorf("no template found for %s", path)
	}
	buf := bytes.Buffer{}
	tmpl := template.Must(template.New("name").Funcs(getFuncMap(config)).Parse(templateStr))
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

func getFuncMap(config *Config) template.FuncMap {
	return template.FuncMap{
		"getPattern": func(l *License) string {
			return l.pattern.String()
		},
		"getText": func(l *License, author string) string {
			return l.matches[author].Text
		},
		"getEscapedText": func(l *License, author string) string {
			return strings.Replace(l.matches[author].Text, "\"", "\\\"", -1)
		},
		"getCategory": func(l *License) string {
			return strings.TrimSuffix(l.Category, ".lic")
		},
		"getFiles": func(l *License, author string) []string {
			var files []string
			for _, file := range l.matches[author].Files {
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
		"getHTMLText": func(m *Match) string {
			txt := m.Text
			txt = strings.ReplaceAll(txt, "<", "&lt;")
			txt = strings.ReplaceAll(txt, ">", "&gt;")
			txt = strings.Replace(txt, "\n", "<br />", -1)
			return txt
		},
		"getMatches": func(l *License) []*Match {
			sortedList := []*Match{}
			for _, m := range l.matches {
				sortedList = append(sortedList, m)
			}
			sort.Sort(matchByText(sortedList))

			return sortedList
		},
		"getFilesFromMatch": func(m *Match) string {
			result := ""
			if config.PrintFiles && len(m.Files) > 0 {
				result += "Files:\n"
				sort.Strings(m.Files)
				for _, s := range m.Files {
					result += " -> " + s + "\n"
				}
			}
			return result
		},
		"getProjectsFromMatch": func(m *Match) string {
			result := ""
			if config.PrintProjects && len(m.Projects) > 0 {
				result += "Projects:\n"
				sort.Strings(m.Projects)
				for _, s := range m.Projects {
					result += " -> " + s + "\n"
				}
			}
			return result
		},
		"getCopyrights": func(m *Match) string {
			sortedList := []string{}
			for c := range m.Copyrights {
				trim := strings.TrimSpace(c)
				if trim != "" {
					sortedList = append(sortedList, trim)
				}
			}
			sort.Strings(sortedList)

			result := ""
			for _, s := range sortedList {
				result += s + "\n"
			}
			return result
		},
	}
}
