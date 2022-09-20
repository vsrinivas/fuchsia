// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package license

import (
	"bytes"
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/check-licenses/file"
)

// Pattern contains a searchable regex pattern for finding license text
// in source files and LICENSE files across the repository.
type Pattern struct {
	// Absolute path to this license pattern file.
	AbsPath string

	// Relative path from the root Fuchsia directory to this license pattern file.
	RelPath string

	// Name is the name of the license pattern file.
	Name string

	// Type is the type of license that this pattern matches.
	// e.g. BSD, MIT, etc..
	// This is set using the name of the parent folder where the pattern file lives.
	Type string

	// Category is the category that this license belongs to.
	// Approved, Restricted, Allowlist_Only
	// This is set using the name of the grandparent folder where the pattern file lives.
	Category string

	// AllowList is a string of regex patterns that match with project paths.
	// This license pattern is only allowed to match with projects listed here.
	AllowList []string

	// Exceptions is a list of structs that hold information about allowlisted projects.
	Exceptions []*Exception

	// Matches maintains a slice of pointers to the data fragments that it matched against.
	// This is used in some templates in the result package, for grouping license texts
	// by pattern type in the resulting NOTICE file.
	Matches []*file.FileData

	// The regex pattern.
	// This is exported so the result package can save the pattern to disk at the end.
	Re *regexp.Regexp

	// Maps that keep track of previous successful and failed
	// searches, keyed using filedata hash.
	PreviousMatches    map[string]bool
	PreviousMismatches map[string]bool

	isHeader bool
}

// Order implements sort.Interface for []*Pattern based on the Name field.
type Order []*Pattern

func (a Order) Len() int           { return len(a) }
func (a Order) Swap(i, j int)      { a[i], a[j] = a[j], a[i] }
func (a Order) Less(i, j int) bool { return a[i].Name < a[j].Name }

// NewPattern returns a Pattern object with the regex pattern loaded from the .lic folder.
// Some preprocessing is done to the pattern (e.g. removing code comment characters).
func NewPattern(path string) (*Pattern, error) {
	b, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}

	// License pattern files end in ".lic", and are essentially giant regex
	// strings. This works and is potentially more flexible, but they are
	// hard to maintain, and hard to tailor to fit multiple license texts
	// from different projects.
	//
	// Here, we now support license texts in their entirety. The following
	// code converts a license text block into a regex pattern by escaping
	// special characters.
	if filepath.Ext(path) == ".txt" {
		b = bytes.ReplaceAll(b, []byte(`\`), []byte(`\\`))
		b = bytes.ReplaceAll(b, []byte(`.`), []byte(`\.`))
		b = bytes.ReplaceAll(b, []byte(`?`), []byte(`\?`))
		b = bytes.ReplaceAll(b, []byte("`"), []byte(`\x60`))
		b = bytes.ReplaceAll(b, []byte(`[`), []byte(`\[`))
		b = bytes.ReplaceAll(b, []byte(`]`), []byte(`\]`))
		b = bytes.ReplaceAll(b, []byte(`(`), []byte(`\(`))
		b = bytes.ReplaceAll(b, []byte(`)`), []byte(`\)`))
		b = bytes.ReplaceAll(b, []byte(`*`), []byte(`\*`))
		b = bytes.ReplaceAll(b, []byte(`+`), []byte(`\+`))

		b = append([]byte(`(`), b...)
		b = append(b, []byte(`)`)...)
	}
	regex := string(b)

	// Remove any duplicate whitespace characters
	regex = strings.Join(strings.Fields(regex), " ")

	// Update regex to ignore multiple white spaces, newlines, comments.
	regex = strings.ReplaceAll(regex, ` `, `(?:[\s\\#\*\/]|\^L)*`)

	// Convert date strings to a regex that supports any date
	dates := regexp.MustCompile(`(\D)[\d]{4}(\D)`)
	regex = dates.ReplaceAllString(regex, `$1[\d]{4}$2`)

	re, err := regexp.Compile(regex)
	if err != nil {
		return nil, fmt.Errorf("%s: %w", path, err)
	}

	name := filepath.Base(path)

	// Retrieve the license type (e.g. MIT, BSD) from the filepath.
	licType := filepath.Base(filepath.Dir(path))

	// Retrieve the license category (e.g. Approved, Restricted) from the filepath.
	licCategory := filepath.Base(filepath.Dir(filepath.Dir(path)))

	allowlist := make([]string, 0)
	if licCategory == "approved" || licCategory == "notice" {
		allowlist = append(allowlist, ".*")
	} else {
		// allowlist_only and restricted
		// TODO: make restricted licenses un-allowlist-able.
		if regexes, ok := AllowListPatternMap[name]; ok {
			allowlist = append(allowlist, regexes...)
		}
	}

	relPath := path
	if filepath.IsAbs(path) {
		relPath, err = filepath.Rel(Config.FuchsiaDir, path)
		if err != nil {
			return nil, err
		}
	}

	absPath, err := filepath.Abs(path)
	if err != nil {
		return nil, err
	}

	exceptions := make([]*Exception, 0)
	for _, e := range Config.Exceptions {
		if e.LicenseType == licType {
			exceptions = append(exceptions, e)
		}
	}

	return &Pattern{
		Name:               name,
		AbsPath:            absPath,
		RelPath:            relPath,
		Type:               licType,
		Category:           licCategory,
		AllowList:          allowlist,
		Exceptions:         exceptions,
		Matches:            make([]*file.FileData, 0),
		PreviousMatches:    make(map[string]bool),
		PreviousMismatches: make(map[string]bool),
		Re:                 re,
	}, nil
}

// Search the given data slice for text that matches this Pattern regex.
func (p *Pattern) Search(d *file.FileData) bool {
	// If the data is empty, and this pattern is "_empty", return true.
	if len(d.Data) == 0 && p.Name == "_empty" {
		return true
	}

	// If we've seen this data segment before, return the previous result.
	// This should be faster than running the regex search.
	if _, ok := p.PreviousMatches[d.Hash()]; ok && !p.isHeader {
		p.Matches = append(p.Matches, d)
		d.LicenseType = p.Type
		return true
	} else if _, ok := p.PreviousMismatches[d.Hash()]; ok {
		return false
	}

	if m := p.Re.Find(d.Data); m != nil {
		// If this is a source file with the copyright header info at the top,
		// modify the filedata object to hold the copyright info instead of the
		// full file contents.
		if p.isHeader {
			d.SetData(m)
		}
		d.LicenseType = p.Type

		p.Matches = append(p.Matches, d)
		p.PreviousMatches[d.Hash()] = true

		return true
	}

	p.PreviousMismatches[d.Hash()] = true
	return false
}
