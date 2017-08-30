// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains fmtCmd which formats a .mojom file and outputs the
// formatted mojom file to stdout.
// The fmt command is invoked as follows:
//
//		mojom fmt [-w] <mojom_file>
//
//	If -w is specified and the formatted file is different from the original,
//	the file is overwritten with the formatted version.
package main

import (
	"fidl/compiler/formatter"
	"flag"
	"fmt"
	"io/ioutil"
	"log"
	"os"
	"path/filepath"
)

// fmtCmd implements the fmt command for the mojom tool.
// The slice of strings |args| is the list of arguments passed on the command
// line starting with the program name and followed by the invoked command.
func fmtCmd(args []string) {
	flagSet := flag.NewFlagSet("fmt", flag.ContinueOnError)
	var overwrite bool
	flagSet.BoolVar(&overwrite, "w", false,
		"Overwrite the specified file with the formatted version if it is different from the original.")

	printUsage := func() {
		fmt.Fprintf(os.Stderr, "Usage: %s fmt [-w] <mojom_file>\n\n", filepath.Base(args[0]))
		fmt.Fprintf(os.Stderr, UsageString(flagSet))
	}

	if err := flagSet.Parse(args[2:]); err != nil {
		if err != flag.ErrHelp {
			fmt.Fprintln(os.Stderr, err.Error())
		}
		printUsage()
		os.Exit(1)
	}

	inputFileName := flagSet.Arg(0)
	if inputFileName == "" {
		fmt.Fprintln(os.Stderr, "No .mojom file given.")
		printUsage()
		os.Exit(1)
	}

	originalBytes, err := ioutil.ReadFile(inputFileName)
	if err != nil {
		log.Fatalln(err.Error())
	}

	original := string(originalBytes[:])

	var formatted string
	formatted, err = formatter.FormatMojom(inputFileName, original)
	if err != nil {
		log.Fatalln(err.Error())
	}

	if overwrite {
		if formatted != original {
			if err := ioutil.WriteFile(inputFileName, []byte(formatted), 0); err != nil {
				log.Fatalln(err.Error())
			}
		}
	} else {
		fmt.Print(formatted)
	}
}
