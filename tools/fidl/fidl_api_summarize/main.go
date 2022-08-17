// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The program fidl_api_summarize extracts FIDL API information from the FIDL
// intermediate representation files.  Please refer to README.md in this
// directory for more details.
package main

import (
	"bytes"
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
	format               = summarize.TextSummaryFormat
)

// TODO(kjharland): Move flags into main().
func init() {
	flag.Var(&format, "format", "Specify the output format (text|json)")
}

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
		fmt.Fprintln(os.Stderr, err.Error())
		os.Exit(1)
	}
}

func mainImpl() error {
	if *fir == "" {
		return fmt.Errorf("The flag --fidl-ir-file=... is required")
	}
	in, err := os.Open(*fir)
	if err != nil {
		return fmt.Errorf("Could not open file: %v: %w", *fir, err)
	}
	if *out == "" {
		return fmt.Errorf("The flag --output-file=... is required")
	}

	root, err := fidlgen.DecodeJSONIr(in)
	if err != nil {
		return fmt.Errorf("Could not parse FIDL IR from: %v: %w", *in, err)
	}

	b, err := summarize.GenerateSummary(root, format)
	if err != nil {
		return fmt.Errorf("While summarizing %v into %v: %w", *in, *out, err)
	}

	if *suppressEmptyLibrary {
		emptyLibRoot := fidlgen.Root{Name: root.Name}
		emptyLibBytes, err := summarize.GenerateSummary(emptyLibRoot, format)
		if err != nil {
			return err
		}
		if bytes.Equal(b, emptyLibBytes) {
			return os.WriteFile(*out, []byte{}, 0644)
		}
	}

	return os.WriteFile(*out, b, 0644)
}
