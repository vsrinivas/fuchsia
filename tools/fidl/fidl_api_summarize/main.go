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
	"io"
	"os"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/summarize"
)

var (
	fir    = flag.String("fidl-ir-file", "", "The FIDL IR input file to produce an API summary for.")
	out    = flag.String("output-file", "", "The output file to write the summary into.")
	format = flag.String("format", "text", "Specify the output format (text|json)")
)

// usage prints a user-friendly usage message when the flag --help is provided.
func usage() {
	fmt.Fprintf(flag.CommandLine.Output(),
		`%v extracts FIDL API information from the FIDL intermediate representation files.

Usage:
`, os.Args[0])
	flag.PrintDefaults()
}

// getWriter returns the appropriate function for writing output, based on the
// format chosen through the --format flag.
func getWriter() (func(fidlgen.Root, io.Writer) error, error) {
	switch *format {
	case "text":
		return summarize.Write, nil
	case "json":
		return summarize.WriteJSON, nil
	default:
		return nil, fmt.Errorf("not a recognized flag value: %v", *format)
	}
}

func main() {
	flag.Usage = usage
	flag.Parse()

	writerFn, err := getWriter()
	if err != nil {
		fmt.Fprintf(os.Stderr, "While parsing --format: %v\n", err)
		os.Exit(1)
	}
	if *fir == "" {
		fmt.Fprintf(os.Stderr, "The flag --fidl-ir-file=... is required\n")
		os.Exit(1)
	}
	in, err := os.Open(*fir)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Could not open file: %v: %v\n", *fir, err)
		os.Exit(1)
	}
	if *out == "" {
		fmt.Fprintf(os.Stderr, "The flag --output-file=... is required\n")
		os.Exit(1)
	}

	w, err := os.Create(*out)
	defer func() {
		if err := w.Close(); err != nil {
			fmt.Fprintf(os.Stderr, "Error while closing: %v: %v\n", *out, err)
			os.Exit(1)
		}
	}()

	if err != nil {
		fmt.Fprintf(os.Stderr, "Could not create file: %v: %v\n", *out, err)
		os.Exit(1)
	}
	root, err := fidlgen.DecodeJSONIr(in)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Could not parse FIDL IR from: %v: %v\n", *in, err)
		os.Exit(1)
	}
	if err := writerFn(root, w); err != nil {
		fmt.Fprintf(os.Stderr, "While summarizing %v into %v: %v\n", *in, *out, err)
		os.Exit(1)
	}
}
