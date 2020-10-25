// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package checklicenses

import (
	"errors"
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strings"
)

// Licenses is an object that facilitates operations on each License object in bulk
type Licenses struct {
	licenses []*License
}

// NewLicenses returns a Licenses object with each license pattern loaded from
// the .lic folder location specified in Config
func NewLicenses(root string, prohibitedLicenseTypes []string) (*Licenses, error) {
	f, err := os.Open(root)
	if err != nil {
		return nil, err
	}
	names, err := f.Readdirnames(0)
	f.Close()
	if err != nil {
		return nil, err
	}

	l := &Licenses{}
	for _, n := range names {
		bytes, err := ioutil.ReadFile(filepath.Join(root, n))
		if err != nil {
			return nil, err
		}
		regex := string(bytes)
		// Skip updating white spaces, newlines, etc for files that end
		// in full.lic since they are larger.
		if !strings.HasSuffix(n, "full.lic") {
			// Update regex to ignore multiple white spaces, newlines, comments.
			regex = strings.ReplaceAll(regex, "\n", `[\s\\#\*\/]*`)
			regex = strings.ReplaceAll(regex, " ", `[\s\\#\*\/]*`)
		}
		re, err := regexp.Compile(regex)
		if err != nil {
			return nil, fmt.Errorf("%s: %w", n, err)
		}
		l.licenses = append(
			l.licenses,
			&License{
				pattern:      re,
				category:     n,
				validType:    contains(prohibitedLicenseTypes, n),
				matches:      map[string]*Match{},
				matchChannel: make(chan *Match, 10),
			})
	}
	if len(l.licenses) == 0 {
		return nil, errors.New("no licenses")
	}
	// Reorder the files putting fuchsia licenses first, then shortest first.
	sort.Slice(l.licenses, func(i, j int) bool {
		a := strings.Contains(l.licenses[i].category, "fuchsia")
		b := strings.Contains(l.licenses[j].category, "fuchsia")
		if a != b {
			return a
		}
		return len(l.licenses[i].pattern.String()) < len(l.licenses[j].pattern.String())
	})
	return l, nil
}

func (l *Licenses) GetFilesWithProhibitedLicenses() []string {
	var filesWithProhibitedLicenses []string
	for _, license := range l.licenses {
		if license.validType {
			continue
		}
		for _, match := range license.matches {
			filesWithProhibitedLicenses = append(filesWithProhibitedLicenses, match.files...)
		}
	}
	return filesWithProhibitedLicenses
}

func (l *Licenses) MatchSingleLicenseFile(data []byte, base string, metrics *Metrics, file_tree *FileTree) {
	// TODO(solomonokinard) deduplicate Match*File()
	for _, license := range l.licenses {
		if m := license.pattern.Find(data); m != nil {
			metrics.increment("num_single_license_file_match")
			path := strings.TrimSpace(file_tree.getPath() + base)
			license.matchAuthors(string(m), data, path)
			file_tree.Lock()
			file_tree.singleLicenseFiles[base] = append(file_tree.singleLicenseFiles[base], license)
			file_tree.Unlock()
		}
	}
}

// MatchFile returns true if any License matches input data.
func (l *Licenses) MatchFile(data []byte, path string, metrics *Metrics) bool {
	for _, license := range l.licenses {
		if m := license.pattern.Find(data); m != nil {
			metrics.increment("num_licensed")
			license.matchAuthors(string(m), data, path)
			return true
		}
	}
	return false
}

func contains(matches []string, item string) bool {
	for _, m := range matches {
		if strings.Contains(item, m) {
			return false
		}
	}
	return true
}
