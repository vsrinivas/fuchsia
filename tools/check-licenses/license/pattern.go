// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package license

import (
	"fmt"
	"io/ioutil"
	"regexp"
	"strings"
	"sync"
)

// Pattern contains a searchable regex pattern for finding license text
// in source files and LICENSE files across the repository.
type Pattern struct {
	re *regexp.Regexp

	sync.Mutex
}

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
		re: re,
	}, nil
}

// Search the given data slice for text that matches this Pattern regex.
func (p *Pattern) Search(data []byte) bool {
	if m := p.re.FindSubmatch(data); m != nil {
		return true
	}
	return false
}
