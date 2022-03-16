// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package license

import (
	"fmt"
	"io/ioutil"
	"path/filepath"
	"regexp"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/check-licenses/file"
)

// Pattern contains a searchable regex pattern for finding license text
// in source files and LICENSE files across the repository.
type Pattern struct {
	Name    string
	Matches []*file.FileData
	re      *regexp.Regexp

	// Maps that keep track of previous successful and failed
	// searches, keyed using filedata hash.
	previousMatches    map[string]bool
	previousMismatches map[string]bool
}

// Order implements sort.Interface for []*Pattern based on the Name field.
type Order []*Pattern

func (a Order) Len() int           { return len(a) }
func (a Order) Swap(i, j int)      { a[i], a[j] = a[j], a[i] }
func (a Order) Less(i, j int) bool { return a[i].Name < a[j].Name }

// NewPattern returns a Pattern object with the regex pattern loaded from the .lic folder.
// Some preprocessing is done to the pattern (e.g. removing code comment characters).
func NewPattern(path string) (*Pattern, error) {
	bytes, err := ioutil.ReadFile(path)
	if err != nil {
		return nil, err
	}
	regex := string(bytes)

	// Update regex to ignore multiple white spaces, newlines, comments.
	// But first, trim whitespace away so we don't include unnecessary
	// comment syntax.
	regex = strings.Trim(regex, "\n ")
	regex = strings.ReplaceAll(regex, "\n", `([\s\\#\*\/]|\^L)*`)
	regex = strings.ReplaceAll(regex, " ", `([\s\\#\*\/]|\^L)*`)

	re, err := regexp.Compile(regex)
	if err != nil {
		return nil, fmt.Errorf("%s: %w", path, err)
	}

	return &Pattern{
		Name:               filepath.Base(path),
		Matches:            make([]*file.FileData, 0),
		previousMatches:    make(map[string]bool),
		previousMismatches: make(map[string]bool),
		re:                 re,
	}, nil
}

// Search the given data slice for text that matches this Pattern regex.
func (p *Pattern) Search(d *file.FileData) bool {
	// If we've seen this data segment before, return the previous result.
	// This should be faster than running the regex search.
	if _, ok := p.previousMatches[d.Hash()]; ok {
		p.Matches = append(p.Matches, d)
		return true
	} else if _, ok := p.previousMismatches[d.Hash()]; ok {
		return false
	}

	if m := p.re.FindSubmatch(d.Data); m != nil {
		p.Matches = append(p.Matches, d)
		p.previousMatches[d.Hash()] = true
		return true
	}
	p.previousMismatches[d.Hash()] = true
	return false
}
