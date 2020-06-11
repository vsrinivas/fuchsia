// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package lib

import (
	"fmt"
	"os"
	"text/template"

	"go.fuchsia.dev/fuchsia/scripts/check-licenses/templates"
)

// Walk gathers all Licenses then checks for a match within each filtered file
func Walk(config *Config) error {
	metrics := new(Metrics)
	file_tree := NewFileTree(config, metrics)
	licenses, err := NewLicenses(config.LicensePatternDir)
	if err != nil {
		return err
	}
	for path := range FileIterator(file_tree) { // file, not singleLicenseFile
		processFile(path, metrics, licenses, config, file_tree)
	}
	file, err := createOutputFile(config)
	if err != nil {
		return err
	}
	saveToOutputFile(file, licenses, config, metrics)
	return nil
}

func createOutputFile(config *Config) (*os.File, error) {
	return os.Create(config.OutputFilePrefix + "." + config.OutputFileExtension)
}

func saveToOutputFile(file *os.File, licenses *Licenses, config *Config, metrics *Metrics) error {
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
		"SiGnAtUrE",
		used,
		unused,
	}
	var templateStr string
	switch config.OutputFileExtension {
	case "txt":
		templateStr = templates.TemplateTxt
	case "htm":
		templateStr = templates.TemplateHtml
	default:
		fmt.Println("error: no template found")
	}
	tmpl := template.Must(template.New("name").Funcs(template.FuncMap{
		"getPattern":  func(license *License) string { return (*license).pattern.String() },
		"getText":     func(license *License) string { return string((*license).matches[0].value) },
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
	fmt.Println("Metrics:")
	fmt.Printf("num_licensed: %d\n", metrics.num_licensed)
	fmt.Printf("num_unlicensed: %d\n", metrics.num_unlicensed)
	fmt.Printf("num_with_project_license %d\n", metrics.num_with_project_license)
	fmt.Printf("num_extensions_excluded: %d\n", metrics.num_extensions_excluded)
	fmt.Printf("num_single_license_files: %d\n", metrics.num_single_license_files)
	fmt.Printf("num_non_single_license_files %d\n", metrics.num_non_single_license_files)
	return nil
}

// FileIterator returns each regular file path from the in memory FileTree
func FileIterator(file_tree *FileTree) <-chan string {
	ch := make(chan string)
	go func() {
		var curr *FileTree
		var q []*FileTree
		q = append(q, file_tree)
		var pos int
		for {
			if len(q) == 0 {
				break
			}
			pos = len(q) - 1
			curr = q[pos]
			q = q[:pos]
			base := curr.getPath()
			for _, file := range curr.files {
				ch <- base + file
			}
			for _, child := range curr.children {
				q = append(q, child)
			}
		}
		close(ch)
	}()
	return ch
}

func processFile(path string, metrics *Metrics, licenses *Licenses, config *Config, file_tree *FileTree) {
	fmt.Printf("visited file or dir: %q\n", path)
	file, err := os.Open(path)
	if err != nil {
		fmt.Printf("error opening: %v\n", err)
	}
	defer file.Close()
	data := make([]byte, config.MaxReadSize)
	count, err := file.Read(data)
	if err != nil {
		fmt.Printf("error reading: %v\n", err)
	}
	is_matched := licenses.MatchFile(data, count, path, metrics)
	if !is_matched {
		project := file_tree.getProjectLicense(path)
		if project == nil {
			metrics.num_unlicensed++
			fmt.Printf("File license: missing. Project license: missing. path: %s\n", path)
		} else {
			// TODO derive one of the project licenses
			metrics.num_with_project_license++
			fmt.Printf("File license: missing. Project license: exists. path: %s\n", path)
		}
	}

	// TODO if no license match, use nearest matching LICENSE file
}

// TODO tools/zedmon/client/pubspec.yaml" error reading: EOF
