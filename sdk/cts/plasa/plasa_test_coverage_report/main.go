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
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"os"
	"path"
	"sort"

	"go.fuchsia.dev/fuchsia/sdk/cts/plasa/model"
)

var (
	plasaManifestFile = flag.String("plasa-manifest-file", "", "The Plasa manifest file to read in")
	output            = flag.String("output", "", "The test coverage report output file")
	fragmentPrefix    = flag.String("fragment-prefix", "", "The path prefixed to each fragment file, usually only needed for tests")
)

// PlasaManifest represents the contents of a Plasa manifest file.
type PlasaManifest []PlasaFragment

// PlasaFragment is a single element of a Plasa manifest
type PlasaFragment struct {
	// Dest is the (optional) destination to which a fragment should be packaged.
	Dest string `json:"dest,omitempty"`
	// File is the GN file label pointing at a fragment.
	File string `json:"file,omitempty"`
	// Kind is the fragment file content type.
	Kind string `json:"kind"`
	// Path is the file path on the local filesystem where the fragment file
	// can be found and loaded from.
	Path string `json:"path"`
}

func readManifest(r io.Reader) (PlasaManifest, error) {
	var m PlasaManifest
	d := json.NewDecoder(r)
	d.DisallowUnknownFields()
	if err := d.Decode(&m); err != nil {
		return m, fmt.Errorf("while decoding plasa manifest: %w", err)
	}
	return m, nil
}

func paths(m PlasaManifest) []string {
	var ret []string
	for _, f := range m {
		s := path.Join(*fragmentPrefix, f.Path)
		ret = append(ret, s)
	}
	return ret
}

func addTo(m *map[string]struct{}, r model.Report) {
	for _, i := range r.Items {
		(*m)[i.Name] = struct{}{}
	}
}

func mapKeys(m map[string]struct{}) []string {
	var s []string
	for k := range m {
		s = append(s, k)
	}
	sort.Strings(s)
	return s
}

func filter(m io.Reader, w io.Writer) error {
	p, err := readManifest(m)
	if err != nil {
		return fmt.Errorf("could not read manifest: %v: %w", plasaManifestFile, err)
	}
	paths := paths(p)
	out := map[string]struct{}{}
	for _, p := range paths {
		pf, err := os.Open(p)
		if err != nil {
			return fmt.Errorf("could not read fragment: %v: %w", p, err)
		}

		r, err := model.ReadReportJSON(pf)
		if err != nil {
			return fmt.Errorf("could not parse report: %v: %w", p, err)
		}
		addTo(&out, r)
	}
	k := mapKeys(out)
	for _, v := range k {
		fmt.Fprintf(w, "%v\n", v)
	}
	return nil
}

func run(plasaManifestFile, output string) error {
	if plasaManifestFile == "" {
		return fmt.Errorf("missing flag: --plasa-manifest-file=...")
	}
	m, err := os.Open(plasaManifestFile)
	if err != nil {
		return fmt.Errorf("could not open: %v: %w", plasaManifestFile, err)
	}
	defer m.Close()
	var w io.WriteCloser

	if output == "" {
		w = os.Stdout
	} else {
		w, err = os.Create(output)
		if err != nil {
			return fmt.Errorf("could not open output: %v: %w", output, err)
		}
		defer w.Close()
	}
	return filter(m, w)
}

func main() {
	flag.Parse()
	if err := run(*plasaManifestFile, *output); err != nil {
		fmt.Fprintf(os.Stderr, "plasa_test_coverage_report/main.run(): %v", err)
		os.Exit(-1)
	}
}
