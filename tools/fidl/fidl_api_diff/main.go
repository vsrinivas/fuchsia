// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The program fidl_api_diff computes  a FIDL API surface diff from the
// FIDL API summary files.  Please refer to README.md in this directory
// for more details.
package main

import (
	"flag"
	"fmt"
	"io"
	"os"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/apidiff"
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/summarize"
)

var (
	beforeFile = flag.String("before-file", "", "The FIDL API summary JSON file for the original API surface")
	afterFile  = flag.String("after-file", "", "The FIDL API summary JSON file for the modified API surface")
	outFile    = flag.String("api-diff-file", "", "The JSON file to write the API diff format into")
	format     = flag.String("format", "text", "Specify the output format (text|json)")
	lenient    = flag.Bool("lenient", false, "If set, the program will always exit with the exit code zero.")
)

// usage prints a user-friendly usage message when the flag --help is provided.
func usage() {
	fmt.Fprintf(flag.CommandLine.Output(),
		`%v extracts FIDL API information from the FIDL intermediate representation files.

Usage:
`, os.Args[0])
	flag.PrintDefaults()
}

type writerFn func(before, after io.Reader, out io.Writer) error

// getWriter selects a writer based on the passed-in format.
func writeReport(d apidiff.Report, w io.Writer) error {
	switch *format {
	case "":
		fallthrough
	case "text":
		return d.WriteText(w)
	case "json":
		return d.WriteJSON(w)
	default:
		return fmt.Errorf("not a recognized flag value for --format: %v", *format)
	}
}

// errExitCode returns the exit code to use on error.  Allows lenient error-free
// exit even in case of an encountered error.
func errExitCode() int {
	if *lenient {
		return 0
	}
	return 1
}

func getReaders(beforeFile, afterFile string) (before, after io.Reader, err error) {
	if beforeFile == "" {
		return nil, nil, fmt.Errorf("The flag --before-file=... is required.")
	}

	before, err = os.Open(beforeFile)
	if err != nil {
		return nil, nil, fmt.Errorf("Error while opening: %v: %w", beforeFile, err)
	}

	if afterFile == "" {
		return nil, nil, fmt.Errorf("The flag --after-file=... is required")
	}
	after, err = os.Open(afterFile)
	if err != nil {
		return nil, nil, fmt.Errorf("Error while opening: %v: %v", afterFile, err)
	}
	return
}

func getWriter(outFile string) (io.WriteCloser, error) {
	if outFile == "" {
		return nil, fmt.Errorf("The flag --api-diff-file=... is required")
	}
	out, err := os.Create(outFile)
	if err != nil {
		return nil, fmt.Errorf("Error while creating: %v: %w", outFile, err)
	}
	return out, nil
}

func main() {
	flag.Usage = usage
	flag.Parse()

	lenient := errExitCode()

	before, after, err := getReaders(*beforeFile, *afterFile)
	if err != nil {
		fmt.Fprint(os.Stderr, err)
		os.Exit(lenient)
	}
	out, err := getWriter(*outFile)
	if err != nil {
		fmt.Fprint(os.Stderr, err)
		os.Exit(lenient)
	}
	defer func() {
		if err := out.Close(); err != nil {
			fmt.Fprintf(os.Stderr,
				"Error while closing: %v: %v", *outFile, err)
			os.Exit(lenient)
		}
	}()

	summaries, err := summarize.LoadSummariesJSON(before, after)
	if err != nil {
		fmt.Fprintf(os.Stderr,
			"Error while loading summaries: %v: %v", *outFile, err)
		os.Exit(lenient)
	}
	bs := summaries[0]
	as := summaries[1]
	report, err := apidiff.Compute(bs, as)
	if err != nil {
		fmt.Fprintf(os.Stderr,
			"Error while computing API diff: %v: %v", *outFile, err)
		os.Exit(lenient)
	}
	if err := writeReport(report, out); err != nil {
		fmt.Fprintf(os.Stderr,
			"Error while diffing:\n\tbefore: %v\n\tafter: %v\n\toutput: %v\n\t%v",
			*beforeFile, *afterFile, *outFile, err)
		os.Exit(lenient)
	}
}
