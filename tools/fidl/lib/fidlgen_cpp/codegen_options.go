// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"fmt"
	"path/filepath"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

type CodegenOptions interface {
	// IncludeStem returns the path suffix after the library path when referencing includes.
	IncludeStem() string

	// IncludeBase returns the directory to which C and C++ includes should be relative.
	IncludeBase() string

	// Header returns the path to the generated library header file.
	Header() string

	// Use the unified binding source layout.
	UnifiedSourceLayout() bool
}

// CalcPrimaryHeader computes the relative path to include the main generated library header.
// The path can then be used in templates like so:
//     #include <{{ PrimaryHeader }}>
func CalcPrimaryHeader(opts CodegenOptions, library fidlgen.LibraryIdentifier) (string, error) {
	headerPath, err := filepath.Abs(opts.Header())
	if err != nil {
		return "", err
	}

	// When IncludeBase is not specified, assume the standard convention for including
	// fidl library dependencies, i.e.
	//     #include <fuchsia/library/name/{include-stem}.h>
	if opts.IncludeBase() == "" {
		var libraryPath = ""
		if opts.UnifiedSourceLayout() {
			libraryPath = fmt.Sprintf("fidl/%s", library)
		} else {
			var parts []string
			for _, part := range library {
				parts = append(parts, string(part))
			}
			libraryPath = filepath.Join(parts...)
		}
		return fmt.Sprintf("%s/%s.h", libraryPath, opts.IncludeStem()), nil
	}

	absoluteIncludeBase, err := filepath.Abs(opts.IncludeBase())
	if err != nil {
		return "", err
	}
	if !strings.HasPrefix(headerPath, absoluteIncludeBase) {
		return "", fmt.Errorf("include-base (%v) is not a parent of header (%v)",
			absoluteIncludeBase, headerPath)
	}
	relStem, err := filepath.Rel(opts.IncludeBase(), opts.Header())
	if err != nil {
		return "", err
	}
	return relStem, nil
}

// CommonFlags are command line flags that every C++ backend should accept.
// They match what's specified by the GN template fidl_cpp_codegen at //build/cpp/fidl_cpp.gni.
type CommonFlags struct {
	Json            *string
	Header          *string
	Source          *string
	IncludeBase     *string
	IncludeStem     *string
	ClangFormatPath *string
}
