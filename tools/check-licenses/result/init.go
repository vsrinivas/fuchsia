// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package result

import (
	"io/fs"
	"path/filepath"
	"text/template"
)

var (
	AllTemplates map[string]*template.Template
)

func init() {
	AllTemplates = make(map[string]*template.Template)
}

func Initialize(c *ResultConfig) error {
	Config = c
	return initializeTemplates()
}

func initializeTemplates() error {
	for _, templateCategory := range Config.Templates {
		for _, templatePath := range templateCategory.Paths {
			templatePath = filepath.Join(Config.FuchsiaDir, templatePath)
			if err := filepath.WalkDir(templatePath, func(currentPath string, info fs.DirEntry, err error) error {
				if err != nil {
					return err
				}

				if !info.IsDir() {
					if temp, err := template.New(filepath.Base(currentPath)).Funcs(templateFunctions).ParseFiles(currentPath); err != nil {
						return err
					} else {
						relPath, err := filepath.Rel(templatePath, currentPath)
						if err != nil {
							return err
						}
						plusVal(NumInitTemplates, currentPath)
						AllTemplates[relPath] = temp
					}
				}
				return nil
			}); err != nil {
				return err
			}
		}
	}
	return nil
}
