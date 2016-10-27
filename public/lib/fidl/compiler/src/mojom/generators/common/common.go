// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package common

import (
	"io"
	"mojom/generated/mojom_files"
)

// common groups together functions which make it easier for generators
// implemented in go to implement the same interface.

// GeneratorConfig is used to specify configuration for a generator.
type GeneratorConfig interface {
	// FileGraph returns a parsed MojomFileGraph.
	FileGraph() *mojom_files.MojomFileGraph

	// OutputDir returns an absolute path in which generator output must be
	// written.
	OutputDir() string

	// SrcRootPath returns an absolute path to the root of the source tree. This
	// is used to compute the relative path from the root of the source tree to
	// mojom files for which output is to be generated.
	SrcRootPath() string

	// GenImports returns true if the generator should generate output files for
	// all of the files in the provided file graph, including those that were not
	// specified on the command line but are only present because they were
	// referenced in an import statement. It returns false if the generator should
	// generate output files only for the files which were explicitly specified
	// (as indicated by the fact that they have a non-empty |SpecifiedFileName|).
	GenImports() bool

	// GenTypeInfo returns true if the generator should generate type information
	// describing the mojom file it is generating for. This type information is a
	// typically a serialized mojom describing all data types defined in a mojom,
	// which can be used for mojom type-introspection.
	GenTypeInfo() bool
}

// Writer is the interface used by the generators to write their output.
// It is also made to faciliate testing by allowing generator code to write
// to a provided buffer such as a bytes.Buffer.
type Writer interface {
	io.Writer
	WriteString(s string) (n int, err error)
}

// writeOutput is the type of the generator function which writes the specified
// file's generated code.
type writeOutput func(fileName string, config GeneratorConfig)

// GenerateOutput iterates through the files in the file graph in |config| and
// for all the files for which an output should be generated, it uses
// writeOutput to write the generated output.
func GenerateOutput(writeOutput writeOutput, config GeneratorConfig) {
	for fileName, file := range config.FileGraph().Files {
		if config.GenImports() || *file.SpecifiedFileName != "" {
			writeOutput(fileName, config)
		}
	}
}
