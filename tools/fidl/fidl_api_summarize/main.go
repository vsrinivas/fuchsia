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
)

var (
	fir = flag.String("fidl-ir-file", "", "The FIDL IR input file to produce an API summary for.")
	out = flag.String("output-file", "", "The output file to write the summary into.")
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

	if *fir == "" {
		fmt.Fprintf(os.Stderr, "The flag --fidl-ir-file=... is required")
		os.Exit(1)
	}
	in, err := os.Open(*fir)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Could not open file: %v: %v", *fir, err)
		os.Exit(1)
	}
	if *out == "" {
		fmt.Fprintf(os.Stderr, "The flag --output-file=... is required")
		os.Exit(1)
	}

	w, err := os.Create(*out)
	defer func() {
		if err := w.Close(); err != nil {
			fmt.Fprintf(os.Stderr, "Error while closing: %v: %v", *out, err)
			os.Exit(1)
		}
	}()

	if err != nil {
		fmt.Fprintf(os.Stderr, "Could not create file: %v: %v", *out, err)
		os.Exit(1)
	}
	root, err := fidlgen.DecodeJSONIr(in)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Could not parse FIDL IR from: %v: %v", *in, err)
		os.Exit(1)
	}
	if err := Summarize(root, w); err != nil {
		fmt.Fprintf(os.Stderr, "While summarizing %v into %v: %v", *in, *out, err)
		os.Exit(1)
	}
}
