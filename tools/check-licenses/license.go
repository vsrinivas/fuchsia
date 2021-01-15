// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package checklicenses

import (
	"fmt"
	"io/ioutil"
	"path/filepath"
	"regexp"
	"sort"
	"strings"
	"sync"
	"unicode"
)

const (
	cp     = `(?: ©| \(C\))`
	date   = `[\d]{4}(?:\s|,|-|[\d]{4})*`
	rights = `All Rights Reserved`
)

var (
	reAuthor = regexp.MustCompile(`(?i)Copyright` + cp + `? ` + date + `(.*)?`)

	reCopyright = [...]*regexp.Regexp{
		regexp.MustCompile(strings.ReplaceAll(
			`(?i)Copyright`+cp+`? `+date+`[\s\\#\*\/]*(.*)(?: -)? `+rights, " ", `[\s\\#\*\/]*`)),
		regexp.MustCompile(`(?i)Copyright` + cp + `? ` + date + `(.*)(?: -)? ` + rights),
		regexp.MustCompile(`(?i)Copyright` + cp + `? ` + date + `(.*)(?:` + rights + `)?`),
		regexp.MustCompile(`(?i)` + cp + ` ` + date + `[\s\\#\*\/]*(.*)(?:-)?`),
		regexp.MustCompile(`(?i)Copyright` + cp + `? (.*?) ` + date),
		regexp.MustCompile(`(?i)Copyright` + cp + `? by (.*) `),
	}

	reAuthors = regexp.MustCompile(`(?i)(?:Contributed|Written|Authored) by (.*) ` + date)
)

// License contains a searchable regex pattern for finding license text
// in source files and LICENSE files across the repository.
type License struct {
	pattern   *regexp.Regexp
	Category  string `json:"category"`
	ValidType bool   `json:"valid license"`

	sync.Mutex
	matches         map[string]*Match
	AllowedDirs     []string
	BadLicenseUsage []string
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

// Match is used to store a single match result alongside the License along
// with a list of all matching files
type Match struct {
	authors    string
	files      []string
	variations map[string]bool
}

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
	regex = strings.ReplaceAll(regex, "\n", `[\s\\#\*\/]*`)
	regex = strings.ReplaceAll(regex, " ", `[\s\\#\*\/]*`)

	re, err := regexp.Compile(regex)
	if err != nil {
		return nil, fmt.Errorf("%s: %w", path, err)
	}

	return &License{
		pattern:         re,
		Category:        filepath.Base(path),
		ValidType:       !contains(config.ProhibitedLicenseTypes, filepath.Base(path)),
		matches:         map[string]*Match{},
		AllowedDirs:     config.LicenseAllowList[filepath.Base(path)],
		BadLicenseUsage: []string{},
	}, nil
}

func (l *License) Search(data []byte, path string) bool {
	if m := l.pattern.Find(data); m != nil {

		// Extract the copyright author information from the text.
		set := getAuthorMatches(data)
		output := make([]string, 0, len(set))
		for key := range set {
			output = append(output, key)
		}

		// Sort the authors alphabetically and join them as one string.
		sort.Strings(output)
		authors := strings.Join(output, ", ")

		// Replace < and > so that it doesn't cause special character highlights.
		authors = strings.ReplaceAll(authors, "<", "&lt")
		authors = strings.ReplaceAll(authors, ">", "&gt")
		blurb := strings.ReplaceAll(string(m), "<", "&lt")
		blurb = strings.ReplaceAll(blurb, ">", "&gt")

		l.Lock()
		if _, ok := l.matches[authors]; !ok {
			variations := make(map[string]bool)
			variations[blurb] = true

			l.matches[authors] = &Match{
				authors:    authors,
				files:      []string{path},
				variations: variations,
			}
		} else {
			l.matches[authors].files = append(l.matches[authors].files, path)
			l.matches[authors].variations[blurb] = true
		}
		l.Unlock()

		return true
	}
	return false
}

func (l *Match) GetText() string {
	// Sort the variations so we always return the same blurb.
	texts := []string{}
	for key := range l.variations {
		texts = append(texts, key)
	}
	sort.Strings(texts)
	return texts[0]
}

func (l *License) Equal(other *License) bool {
	if l.pattern.String() != other.pattern.String() {
		return false
	}
	if l.Category != other.Category {
		return false
	}
	if l.ValidType != other.ValidType {
		return false
	}
	return true
}

// getAuthorMatches returns contributors and authors.
func getAuthorMatches(data []byte) map[string]struct{} {
	set := map[string]struct{}{}
	for _, re := range reCopyright {
		if m := re.FindAllSubmatch(data, -1); m != nil {
			for _, author := range m {
				// Remove nonletters or '>' from the beginning and end of string.
				a := strings.TrimFunc(string(author[1]), func(r rune) bool {
					return !(unicode.IsLetter(r) || r == '>')
				})
				set[a] = struct{}{}
			}
			break
		}
	}
	if m := reAuthors.FindAllSubmatch(data, -1); m != nil {
		for _, author := range m {
			// Remove nonletters or '>' from the beginning and end of string.
			a := strings.TrimFunc(string(author[1]), func(r rune) bool {
				return !(unicode.IsLetter(r) || r == '>')
			})
			set[a] = struct{}{}
		}
	}
	return set
}
