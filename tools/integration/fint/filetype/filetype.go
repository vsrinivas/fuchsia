// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package filetype

import (
	"path/filepath"
)

// FileType represents the language in which a file is written, for the purposes
// of determining whether it can be used for build graph analysis.
type FileType int

const (
	Unknown FileType = iota
	CMake
	ComponentManifest
	CPP
	Dart
	FIDL
	GN
	Go
	Golden
	Markdown
	Owners
	PortableNetworkGraphics
	ProtocolBuffer
	Python
	RestructuredText
	Rust
	SerializedProtocolBuffer
	Shell
	YAML
)

// A from mapping file extensions to file types.
// ".h" is intentionally omitted because we can't guarantee they're properly
// listed in .gn files.
var extToFileType = map[string]FileType{
	".c":        CPP,
	".cc":       CPP,
	".cml":      ComponentManifest,
	".cmake":    CMake,
	".cmx":      ComponentManifest,
	".cpp":      CPP,
	".dart":     Dart,
	".fidl":     FIDL,
	".gn":       GN,
	".gni":      GN,
	".go":       Go,
	".golden":   Golden,
	".md":       Markdown,
	".pb":       SerializedProtocolBuffer,
	".png":      PortableNetworkGraphics,
	".proto":    ProtocolBuffer,
	".py":       Python,
	".rs":       Rust,
	".rst":      RestructuredText,
	".sh":       Shell,
	".template": Markdown,
	".tmpl":     Markdown,
	".yaml":     YAML,
}

// TypeForFile returns the FileType for a file, based on its path.
func TypeForFile(path string) FileType {
	if filepath.Base(path) == "OWNERS" {
		return Owners
	}
	t, ok := extToFileType[filepath.Ext(path)]
	if !ok {
		return Unknown
	}
	return t
}

// KnownFileTypes returns a list of all file types recognized by this library.
func KnownFileTypes() []FileType {
	uniqueTypes := map[FileType]bool{Owners: true}
	for _, ft := range extToFileType {
		uniqueTypes[ft] = true
	}
	var res []FileType
	for ft := range uniqueTypes {
		res = append(res, ft)
	}
	return res
}
