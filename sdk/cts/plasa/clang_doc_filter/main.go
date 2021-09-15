// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file

// This program converts a directory of YAML reports produced by `clang_doc` into
// a report usable by test coverage.
//
// Please refer to the file README.md in this directory for more information
// about the program and its use.

package main

import (
	"flag"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"
)

// fullName reconstructs the fully qualified name of an identifier, given its
// unqualified name and namespace components.
func fullName(name Name, ns []ID) string {
	s := make([]string, len(ns)+1)
	for i, n := range ns {
		nc := string(n.Name)
		if nc == "GlobalNamespace" {
			nc = ""
		}
		s[len(s)-i-2] = nc
	}
	s[len(s)-1] = string(name)
	return strings.Join(s, "::")
}

// addAllFromDir scans inputDir for YAML aggregate files and extracts information from them.
// The information is funneled into the supplied function 'add'.  The function 'add' is allowed
// to fail, in which case the scanning is stopped.
func addAllFromDir(inputDir string, addFn func(a Aggregate) error) error {
	err := filepath.WalkDir(inputDir, func(path string, d os.DirEntry, err error) error {
		if err != nil {
			return fmt.Errorf("filepath.WalkDir called with err != nil:\n\t%w", err)
		}
		if d.IsDir() {
			return nil
		}
		f, err := os.Open(path)
		if err != nil {
			return fmt.Errorf("while opening: %v:\n\t%w", path, err)
		}
		a, err := ParseYAML(f)
		if err != nil {
			return fmt.Errorf("while parsing: %v:\n\t%w", path, err)
		}
		if err := addFn(a); err != nil {
			return fmt.Errorf("while adding aggregate: %+v:\n\t%w", a, err)
		}
		return nil
	})
	if err != nil {
		return fmt.Errorf("somewhere in the input directory: %v\n\t%w", inputDir, err)
	}
	return nil
}

// run executes the report generator on all files in the directory inputDir,
// and writes a report into the supplied writer.
func run(inputDir string, w io.Writer, args args) error {
	var r Report
	r.setFileRegexes(args.allowlistFilenameRegexp)
	r.setSymRegexes(args.allowlistNameRegexp)
	if err := addAllFromDir(inputDir, r.Add); err != nil {
		return fmt.Errorf("in main.run(): while reading dir:\n\t%w", err)
		os.Exit(-1)
	}
	if err := r.writeJSON(w); err != nil {
		return fmt.Errorf("in main.run(): while writing output file:\n\t%w", err)
	}
	return nil
}

// args contains the settings which will be initialized from command line arguments.
type args struct {
	inputDir                string
	outFile                 string
	allowlistFilenameRegexp []string
	allowlistNameRegexp     []string
}

func (a *args) addAllowlistFnRegexp(s string) error {
	a.allowlistFilenameRegexp = append(a.allowlistFilenameRegexp, s)
	return nil
}

func (a *args) addAlowlistSymRegexp(s string) error {
	a.allowlistNameRegexp = append(a.allowlistNameRegexp, s)
	return nil
}

func main() {
	var args args
	flag.StringVar(&args.inputDir, "input-dir", "", "the input directory to get the files from")
	flag.StringVar(&args.outFile, "output-file", "", "the file to write the final report to")
	flag.Func("allow-filename-regexp",
		"a regexp that may match any part of the filename; if unset, no filtering will take place",
		args.addAllowlistFnRegexp)
	flag.Func("allow-symbol-name-regexp",
		"a regexp that may match any part of the symbol name; if unset, no filtering will take place",
		args.addAllowlistFnRegexp)
	flag.Parse()

	if args.inputDir == "" {
		fmt.Fprintf(os.Stderr, "the flag --input-dir=... is required")
		os.Exit(-1)
	}
	if args.outFile == "" {
		fmt.Fprintf(os.Stderr, "the flag --output-file=... is required")
		os.Exit(-1)
	}
	of, err := os.Create(args.outFile)
	if err != nil {
		fmt.Fprintf(os.Stderr, "error while opening output file: %v:\n\t%v", args.outFile, err)
		os.Exit(-1)
	}
	if err := run(args.inputDir, of, args); err != nil {
		fmt.Fprintf(os.Stderr, "error while writing output file: %v:\n\t%v", args.outFile, err)
		os.Exit(-1)
	}
}
