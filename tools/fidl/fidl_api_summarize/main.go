// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The program fidl_api_summarize extracts FIDL API information from the FIDL
// intermediate representation files.  Please refer to README.md in this
// directory for more details.
package main

import (
	"flag"
	"fmt"
	"os"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/summarize"
)

var (
	fir                  = flag.String("fidl-ir-file", "", "The FIDL IR input file to produce an API summary for.")
	out                  = flag.String("output-file", "", "The output file to write the summary into.")
	suppressEmptyLibrary = flag.Bool("suppress-empty-library", false, "Generate empty output for libraries with no declarations")
)

// usage prints a user-friendly usage message when the flag --help is provided.
func usage() {
	fmt.Fprintf(flag.CommandLine.Output(),
		`%v extracts FIDL API information from the FIDL intermediate representation files.

Usage:
`, os.Args[0])
	flag.PrintDefaults()
}

func main() {
	flag.Usage = usage
	flag.Parse()

	if err := mainImpl(); err != nil {
		fmt.Fprintf(os.Stderr, "Error: %s", err)
		os.Exit(1)
	}
}

func mainImpl() error {
	if *fir == "" {
		return fmt.Errorf("missing required flag --fidl-ir-file")
	}
	if *out == "" {
		return fmt.Errorf("missing required flag --output-file=... ")
	}

	root, err := fidlgen.ReadJSONIr(*fir)
	if err != nil {
		return err
	}

	outFile, err := os.Create(*out)
	if err != nil {
		return fmt.Errorf("creating output file: %w", err)
	}
	defer outFile.Close()

	summary := summarize.Summarize(root)
	if *suppressEmptyLibrary && summary.IsEmptyLibrary() {
		// Leave the output file empty.
		return nil
	}
	if err := summary.WriteJSON(outFile); err != nil {
		return fmt.Errorf("writing JSON to %s: %w", *out, err)
	}

	return nil
}
