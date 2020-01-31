// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"strings"

	"fidl/compiler/backend/types"
)

// flagsDef defines the LLCPP flags schema.
type flagsDef struct {
	jsonPath    *string
	header      *string
	source      *string
	includeBase *string
}

var flags = func() flagsDef {
	return flagsDef{
		jsonPath: flag.String("json", "",
			"relative path to the FIDL intermediate representation."),
		header: flag.String("header", "",
			"the output path for the generated header."),
		source: flag.String("source", "",
			"the output path for the generated C++ implementation."),
		includeBase: flag.String("include-base", "",
			"[optional] the directory relative to which includes will be computed. "+
				"If omitted, assumes #include <fidl/library/name/llcpp/fidl.h>"),
	}
}()

// valid returns true if the parsed flags are valid.
func (f flagsDef) valid() bool {
	return *f.jsonPath != "" && *f.header != "" && *f.source != ""
}

type config struct {
	fidl              types.Root
	headerPath        string
	sourcePath        string
	primaryHeaderPath string
}

func (f flagsDef) getConfig() (config, error) {
	fidl, err := types.ReadJSONIr(*f.jsonPath)

	headerPath, err := filepath.Abs(*f.header)
	if err != nil {
		return config{}, err
	}

	sourcePath, err := filepath.Abs(*f.source)
	if err != nil {
		return config{}, err
	}

	var primaryHeaderPath string
	if *f.includeBase != "" {
		absoluteIncludeBase, err := filepath.Abs(*f.includeBase)
		if err != nil {
			return config{}, err
		}
		if !strings.HasPrefix(headerPath, absoluteIncludeBase) {
			return config{}, fmt.Errorf("include-base (%v) is not a parent of header (%v)", absoluteIncludeBase, headerPath)
		}
		relStem, err := filepath.Rel(*f.includeBase, *f.header)
		if err != nil {
			return config{}, err
		}
		primaryHeaderPath = relStem
	} else {
		// Assume the convention for including fidl library dependencies, i.e.
		// #include <fuchsia/library/name/llcpp/fidl.h>
		var parts []string
		for _, part := range fidl.Name.Parts() {
			parts = append(parts, string(part))
		}
		parts = append(parts, "llcpp", "fidl.h")
		primaryHeaderPath = filepath.Join(parts...)
	}

	return config{
		fidl:              fidl,
		headerPath:        headerPath,
		sourcePath:        sourcePath,
		primaryHeaderPath: primaryHeaderPath,
	}, nil
}

func main() {
	flag.Parse()
	if !flag.Parsed() || !flags.valid() {
		flag.PrintDefaults()
		os.Exit(1)
	}

	config, err := flags.getConfig()
	if err != nil {
		log.Fatalf("Error getting config: %v", err)
	}

	generator := newGenerator()
	err = generator.generateFidl(config)
	if err != nil {
		log.Fatalf("Error running generator: %v", err)
	}
}
