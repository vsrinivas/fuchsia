// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package rustgen

import (
	"log"
	"path/filepath"
	"strings"

	"mojom/generated/mojom_files"
)

// Describes a module file for organizing imports
type ModuleTemplate struct {
	// Submodules that exist as files
	Files      []string
	// Submodules that exist as directories
	Submodules []string
}

func NewModuleTemplate(m *Module) ModuleTemplate {
	var submodules []string
	for mod := range m.Submodules {
		submodules = append(submodules, mod)
	}
	return ModuleTemplate{
		Files: m.Files,
		Submodules: submodules,
	}
}

// A recursive tree-like structure that encodes namespace sharing in Mojoms.
// It is used to convert these namespaces into Rust modules.
type Module struct {
	// Submodules that exist as files
	Files      []string
	// Submodules that exist as directories
	Submodules map[string]*Module
}

// Creates a new Module node in the tree
func NewModule() (m *Module) {
	m = new(Module)
	m.Files = make([]string, 0)
	m.Submodules = make(map[string]*Module)
	return
}

// Adds a new module file into the tree in the appropriate place
func (m *Module) Add(namespace []string, file string) {
	if len(namespace) == 0 {
		m.Files = append(m.Files, file)
	} else {
		if m.Submodules[namespace[0]] == nil {
			m.Submodules[namespace[0]] = NewModule()
		}
		m.Submodules[namespace[0]].Add(namespace[1:], file)
	}
}

// Converts a namespace such as "my_module.your_module" into ["my_module", "your_module"]
func GetNamespace(mojomFile *mojom_files.MojomFile) []string {
	if mojomFile.ModuleNamespace != nil {
		return strings.Split(*mojomFile.ModuleNamespace, ".")
	}
	return make([]string, 0)
}

// Figure out what the file we're importing is in the file graph
func fileFromImport(fileGraph *mojom_files.MojomFileGraph, srcRootPath, mojomImport string) mojom_files.MojomFile {
	// rebase path against srcRootPath
	rel_path, err := filepath.Rel(srcRootPath, mojomImport)
	if err != nil {
		log.Fatalf("Cannot determine relative path for '%s'", mojomImport)
	}
	// get the absolute path, which should be equivalent to the file name
	file, err := filepath.Abs(rel_path)
	if err != nil {
		log.Fatalf("Cannot determine absolute path for '%s'", mojomImport)
	}
	return fileGraph.Files[file]
}

// stripExt removes the extension of a file.
func stripExt(fileName string) string {
	return fileName[:len(fileName)-len(filepath.Ext(fileName))]
}

// Convert a file name (with full absolute path) into a bare name that
// is equivalent to what the Rust module name would be.
func RustModuleName(fileName string) string {
	return stripExt(filepath.Base(fileName))
}

// Given the current file and the imported file, we figure out what that Rust
// import path looks like and return it.
func rustImportPath(currentFile *mojom_files.MojomFile, importedFile *mojom_files.MojomFile) string {
	imported_namespace := GetNamespace(importedFile)
	current_namespace := GetNamespace(currentFile)
	var limit int
	if len(imported_namespace) < len(current_namespace) {
		limit = len(imported_namespace)
	} else {
		limit = len(current_namespace)
	}
	last_shared_index := 0
	for i := 0; i < limit; i++ {
		if current_namespace[i] != imported_namespace[i] {
			break;
		}
		last_shared_index = i
	}
	// First super is to leave the current module, which we will
	// need to do no matter what on an import
	import_path := "super::"
	for i := last_shared_index; i < len(current_namespace); i++ {
		import_path = import_path + "super::"
	}
	for i := last_shared_index; i < len(imported_namespace); i++ {
		import_path = import_path + imported_namespace[i] + "::"
	}
	return import_path + RustModuleName(importedFile.FileName)
}

