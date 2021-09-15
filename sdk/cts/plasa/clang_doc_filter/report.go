// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file

package main

import (
	"encoding/json"
	"fmt"
	"io"
	"regexp"
	"sort"
	"strings"
)

// ReportItem is the format of a single report item. Expect this struct will
// grow over time to include more extracted information.
type ReportItem struct {
	Name string `json:"name"`
	// Filename is the file name in which the report item can be found.
	Filename string `json:"file,omitempty"`
	// Line is the line number (1-based) in Filename where the report item can be found.
	LineNumber int `json:"line,omitempty"`
}

// Report is the format of the output report for the clang doc filter.
type Report struct {
	Items []ReportItem `json:"items"`

	// fileregex contains the file regexes to include when adding file names to report, if set.
	// If unset, it has no effect.
	fileregex []regexp.Regexp

	// symregex contains the file regexes to include when adding symbol names to report, if set.
	// If unset, it has no effect.
	symregex []regexp.Regexp
}

// setFileRegexes sets the regular expressions used to match the filename of
// the symbols.  Only symbols that match at least one regexp will be included,
// if the regexp is specified.  Otherwise, everything matches.
func (r *Report) setFileRegexes(regexes []string) error {
	return addRegexes(&r.fileregex, regexes)
}

func (r *Report) setSymRegexes(regexes []string) error {
	return addRegexes(&r.symregex, regexes)
}

// addRegexes compiles and adds regexes to a list.
func addRegexes(r *[]regexp.Regexp, regexes []string) error {
	for _, rx := range regexes {
		rc, err := regexp.Compile(rx)
		if err != nil {
			return fmt.Errorf("could not parse regexp: %+v: %w", r, err)
		}
		*r = append(*r, *rc)
	}
	return nil
}

func (r *Report) matchAnyFilename(fn string) bool {
	return matchAnySymbolToRegex(fn, r.fileregex)
}

func (r *Report) matchAnySymbol(sym string) bool {
	return matchAnySymbolToRegex(sym, r.symregex)
}

func matchAnySymbolToRegex(sym string, regexes []regexp.Regexp) bool {
	if len(regexes) == 0 {
		// If we defined no regexes, match everything.
		return true
	}
	for _, rx := range regexes {
		if rx.Match([]byte(sym)) {
			return true
		}
	}
	return false
}

// Add inserts an Aggregate into the report.
func (r *Report) Add(a Aggregate) error {
	for _, f := range a.ChildFunctions {
		fullName := f.fullName()
		fn := f.DefLocation.Filename
		if !r.matchAnyFilename(fn) || !r.matchAnySymbol(fullName) {
			continue
		}
		i := ReportItem{
			Name:       fullName,
			Filename:   fn,
			LineNumber: f.DefLocation.LineNumber,
		}
		r.Items = append(r.Items, i)
	}
	return nil
}

// WriteJSON writes the contents of the report in JSON format to the supplied
// writer.
func (r Report) writeJSON(w io.Writer) error {
	// Ensure the output is stable.
	if r.Items != nil {
		sort.SliceStable(r.Items, func(i, j int) bool {
			return strings.ToLower(r.Items[i].Name) < strings.ToLower(r.Items[j].Name)
		})
	}
	e := json.NewEncoder(w)
	// We do not expect to need HTML escaping.
	e.SetEscapeHTML(false)
	// This indentation format is compatible with `fx format-code`.
	e.SetIndent("", "    ")
	if err := e.Encode(r); err != nil {
		return fmt.Errorf("while encoding JSON output: %w", err)
	}
	return nil
}

// readReportJSON reads the contents of the report in JSON format from the
// supplied reader.
func readReportJSON(r io.Reader) (Report, error) {
	d := json.NewDecoder(r)
	// Verifying the parsing gets confusing if we're lenient about unknown
	// fields.
	d.DisallowUnknownFields()
	var ret Report
	if err := d.Decode(&ret); err != nil {
		return ret, fmt.Errorf("while reading Report as JSON: %w:", err)
	}
	return ret, nil
}
