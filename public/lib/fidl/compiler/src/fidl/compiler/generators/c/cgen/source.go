// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package cgen

import (
	"fidl/compiler/generated/fidl_files"
)

// Some of these *Template structs are defined in header.go.
// TODO(vardhan): There is more information here than required. Trim it down?
type SourceTemplate struct {
	HeaderFile string
	Imports    []string
	TypeTable  TypeTableTemplate
	Structs    []StructTemplate
	Interfaces []InterfaceTemplate
	Constants  []ConstantTemplate
}

// Since the HeaderTemplate already computes TypeTable (etc.), we should just
// re-use them here.
func NewSourceTemplate(fileGraph *fidl_files.FidlFileGraph,
	file *fidl_files.FidlFile, srcRootPath string, headerTmpl *HeaderTemplate) SourceTemplate {
	return SourceTemplate{
		HeaderFile: mojomToCFilePath(srcRootPath, file.FileName),
		Imports:    headerTmpl.Imports,
		TypeTable:  headerTmpl.TypeTable,
		Structs:    headerTmpl.Structs,
		Interfaces: headerTmpl.Interfaces,
		Constants:  headerTmpl.Constants,
	}
}
