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

// License contains a searchable regex pattern for finding file matches in
// tree.
//
// The Category field is the .lic name
type License struct {
	pattern   *regexp.Regexp
	Category  string `json:"category"`
	ValidType bool   `json:"valid license"`

	mu           sync.Mutex
	matches      map[string]*Match
	matchChannel chan *Match
}

// Match is used to store a single match result alongside the License along
// with a list of all matching files
type Match struct {
	authors string
	value   string
	files   []string
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
		pattern:      re,
		Category:     filepath.Base(path),
		ValidType:    contains(config.ProhibitedLicenseTypes, filepath.Base(path)),
		matches:      map[string]*Match{},
		matchChannel: make(chan *Match, 10),
	}, nil
}

func (l *License) AddMatch(m *Match) {
	l.matchChannel <- m
}

func (l *License) MatchChannelWorker() {
	for m := range l.matchChannel {
		if m == nil {
			break
		}
		l.mu.Lock()
		if _, ok := l.matches[m.authors]; !ok {
			// Replace < and > so that it doesn't cause special character highlights.
			p := strings.ReplaceAll(m.value, "<", "&lt")
			p = strings.ReplaceAll(p, ">", "&gt")
			l.matches[m.authors] = &Match{authors: m.authors, value: p, files: m.files}
		} else {
			l.matches[m.authors].files = append(l.matches[m.authors].files, m.files...)
		}
		l.mu.Unlock()
	}
}

func (l *License) matchAuthors(matched string, data []byte, path string) {
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

	newMatch := &Match{
		authors: authors,
		value:   matched,
		files:   []string{path},
	}
	l.AddMatch(newMatch)
}

const cp = `(?: ©| \(C\))`
const date = `[\d]{4}(?:\s|,|-|[\d]{4})*`
const rights = `All Rights Reserved`

var reAuthor = regexp.MustCompile(`(?i)Copyright` + cp + `? ` + date + `(.*)?`)

var reCopyright = [...]*regexp.Regexp{
	regexp.MustCompile(strings.ReplaceAll(
		`(?i)Copyright`+cp+`? `+date+`[\s\\#\*\/]*(.*)(?: -)? `+rights, " ", `[\s\\#\*\/]*`)),
	regexp.MustCompile(`(?i)Copyright` + cp + `? ` + date + `(.*)(?: -)? ` + rights),
	regexp.MustCompile(`(?i)Copyright` + cp + `? ` + date + `(.*)(?:` + rights + `)?`),
	regexp.MustCompile(`(?i)` + cp + ` ` + date + `[\s\\#\*\/]*(.*)(?:-)?`),
	regexp.MustCompile(`(?i)Copyright` + cp + `? (.*?) ` + date),
	regexp.MustCompile(`(?i)Copyright` + cp + `? by (.*) `),
}

var reAuthors = regexp.MustCompile(`(?i)(?:Contributed|Written|Authored) by (.*) ` + date)

func parseAuthor(l string) string {
	m := reAuthor.FindStringSubmatch(l)
	if m == nil {
		return ""
	}
	a := strings.TrimSpace(m[1])
	const rights = " All rights reserved"
	if strings.HasSuffix(a, rights) {
		a = a[:len(a)-len(rights)]
	}
	return a
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
