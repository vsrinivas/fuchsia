// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package checklicenses

import (
	"fmt"
	"io/ioutil"
	"path/filepath"
	"regexp"
	"strings"
	"sync"
)

// License contains a searchable regex pattern for finding license text
// in source files and LICENSE files across the repository.
type License struct {
	pattern         *regexp.Regexp
	matches         map[string]*Match
	Category        string `json:"category"`
	ValidType       bool   `json:"valid license"`
	AllowedDirs     []string
	BadLicenseUsage []string

	sync.Mutex
}

// licenseByPattern implements sort.Interface for []*License based on the length of the Pattern field.
// Licenses with a "fuchsia" category are sorted above all other licenses.
type licenseByPattern []*License

func (a licenseByPattern) Len() int      { return len(a) }
func (a licenseByPattern) Swap(i, j int) { a[i], a[j] = a[j], a[i] }
func (a licenseByPattern) Less(i, j int) bool {
	l := strings.Contains(a[i].Category, "fuchsia")
	r := strings.Contains(a[j].Category, "fuchsia")
	if l != r {
		return l
	}
	return len(a[i].pattern.String()) < len(a[j].pattern.String())
}

// NewLicense returns a License object with the regex pattern loaded from the .lic folder.
// Some preprocessing is done to the pattern (e.g. removing code comment characters).
func NewLicense(path string, config *Config) (*License, error) {
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

	isValid := !(contains(config.ProhibitedLicenseTypes, filepath.Base(path)) || contains(config.ProhibitedLicenseTypes, filepath.Dir(path)))

	return &License{
		pattern:         re,
		Category:        filepath.Base(path),
		ValidType:       isValid,
		matches:         map[string]*Match{},
		AllowedDirs:     config.LicenseAllowList[filepath.Base(path)],
		BadLicenseUsage: []string{},
	}, nil
}

// NOTICE files currently need to be processed differently compared to regular single-license files.
// This custom license type allows us to collect and present them properly in the final output file.
func NewCustomLicense(name string) *License {
	regex := "(?s)(?P<text>.*)"
	re, _ := regexp.Compile(regex)

	return &License{
		pattern:         re,
		Category:        "custom",
		ValidType:       true,
		matches:         map[string]*Match{},
		BadLicenseUsage: []string{},
	}
}

func (l *License) CheckAllowList(path string) bool {
	allowed := true
	if len(l.AllowedDirs) > 0 && !contains(l.AllowedDirs, path) {
		allowed = false
		if l.ValidType {
			l.BadLicenseUsage = append(l.BadLicenseUsage, path)
		}
	}
	return allowed
}

// Search the given data slice for text that matches this License pattern.
func (l *License) Search(data []byte, file string) (bool, *Match) {
	if m := l.pattern.FindSubmatch(data); m != nil {
		l.CheckAllowList(file)
		match := NewMatch(m, file, l.pattern)
		l.Lock()
		key := hash(match.Text)
		if l.matches[key] == nil {
			l.matches[key] = match
		} else {
			l.matches[key].merge(match)
			match = l.matches[key]
		}
		l.Unlock()
		return true, match
	}
	return false, nil
}

// Many license texts differ by whitespaces, and are otherwise identical.
// This hash function lets us tweak how we dedup the license texts.
func hash(s string) string {
	// For the sake of hashing, ignore code comment characters ('#', '//')
	s = strings.ReplaceAll(s, "#", " ")
	s = strings.ReplaceAll(s, "//", " ")

	// Ignore bullet point markings ('*', '-')
	s = strings.ReplaceAll(s, "*", " ")
	s = strings.ReplaceAll(s, "-", " ")

	return strings.Join(strings.Fields(s), " ")
}
