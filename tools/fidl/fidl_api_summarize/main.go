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

	if err := mainImpl(); err != nil {
		fmt.Fprintln(os.Stderr, err.Error())
		os.Exit(1)
	}
}

func mainImpl() error {
	writerFn, err := getWriter()
	if err != nil {
		return fmt.Errorf("While parsing --format: %v", err)
	}
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

	f, err := os.Create(*out)
	if err != nil {
		return fmt.Errorf("Could not create file: %v: %w", *out, err)
	}

	root, err := fidlgen.DecodeJSONIr(in)
	if err != nil {
		f.Close() // decode error takes precedence.
		return fmt.Errorf("Could not parse FIDL IR from: %v: %w", *in, err)
	}
	if err := writerFn(root, f); err != nil {
		f.Close() // writerFn error takes precedence.
		return fmt.Errorf("While summarizing %v into %v: %w", *in, *out, err)
	}

	return f.Close()
}
