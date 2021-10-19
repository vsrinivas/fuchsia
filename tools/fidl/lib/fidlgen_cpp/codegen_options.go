// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"flag"
	"log"
	"os"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

type experiments []string

func (e *experiments) String() string {
	return strings.Join(*e, ", ")
}

func (e *experiments) Set(value string) error {
	*e = append(*e, value)
	return nil
}

// CmdlineFlags are the common command-line flags for all C++ backends.
type CmdlineFlags struct {
	// Flags

	// json is the path to the JSON IR.
	json string
	// root is the directory that bindings will be generated under.
	root string
	// clangFormatPath is the path to the clang-format binary.
	clangFormatPath string
	// Experiments is a list of experiments that are enabled.
	experiments experiments

	// Configuration

	// Name is the name of the bindings being generated.
	name string
	// validExperiments is the list of supported experiments in this generator
	validExperiments []string
}

// NewCmdlineFlags returns a new instance of CmdlineFlags, which holds the
// values for flags passed on the command-line to a C++ generating fidlgen.
// |name| is the name of the binding used in @bindings_denylist and elsewhere.
// |validExperiments| is a list of experiment names that are supported.
func NewCmdlineFlags(name string, validExperiments []string) *CmdlineFlags {
	flags := CmdlineFlags{name: name, validExperiments: validExperiments}
	flag.StringVar(&flags.json, "json", "",
		"path to the FIDL intermediate representation.")
	flag.StringVar(&flags.root, "root", "",
		"where to generate the bindings.")
	flag.StringVar(&flags.clangFormatPath, "clang-format-path", "",
		"path to the clang-format tool.")
	if len(validExperiments) > 0 {
		flag.Var(&flags.experiments, "experiment",
			"turn on an experiment, one of: "+strings.Join(validExperiments, ", "))
	}

	return &flags
}

func (c *CmdlineFlags) ParseAndLoadIR() *Root {
	flag.Parse()
	if !flag.Parsed() {
		flag.PrintDefaults()
		os.Exit(1)
	}

	// Check that --json is specified.
	if c.json == "" {
		log.Fatal("Missing required flag: --json")
	}

	// Read in JSON IR.
	// We need to do this to determine the FIDL library that we're generating.
	ir, err := fidlgen.ReadJSONIr(c.json)
	if err != nil {
		log.Fatalf("Failed to read JSON IR from %s: %v", c.json, err)
	}

	// Check that all Experiments are in validExperiments.
	// These are always going to be small lists so linear searches are fine.
	for _, e := range c.experiments {
		valid := false
		for _, v := range c.validExperiments {
			if e == v {
				valid = true
				break
			}
		}
		if !valid {
			log.Fatalf("Invalid experiment %s. Must be one of %s", e,
				strings.Join(c.validExperiments, ", "))
		}
	}

	// Check that --root is specified.
	if c.root == "" {
		log.Fatal("Missing required flag: --root")
	}

	return compileFor(ir, c.name)
}

func (c *CmdlineFlags) ExperimentEnabled(experiment string) bool {
	for _, e := range c.experiments {
		if e == experiment {
			return true
		}
	}
	return false
}
