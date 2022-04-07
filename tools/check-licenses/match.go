// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package checklicenses

import (
	"regexp"
	"sync"
)

// Match is used to combine Copyright information, license text and the list of files where they occur
// all in one data struct.
type Match struct {
	Copyrights            map[string]bool
	Text                  string
	Projects              map[string]bool
	Files                 map[string]bool
	LicenseAppliesToFiles map[string]bool
	Used                  bool

	sync.RWMutex
}

// matchByText implements sort.Interface for []*Match based on the Text field.
type matchByText []*Match

func (a matchByText) Len() int           { return len(a) }
func (a matchByText) Swap(i, j int)      { a[i], a[j] = a[j], a[i] }
func (a matchByText) Less(i, j int) bool { return a[i].Text < a[j].Text }

// NewMatch creates a new match data struct from the result of regex.FindSubMatch().
func NewMatch(data [][]byte, file string, pattern *regexp.Regexp) *Match {
	// We can use regex capture groups in golang, and retrieve them by name (e.g. "copyright") by storing the group indices in a map.
	regexMap := map[string][]byte{}
	for i, name := range pattern.SubexpNames() {
		if i != 0 && name != "" {
			regexMap[name] = data[i]
		}
	}

	m := &Match{
		Copyrights:            map[string]bool{string(regexMap["copyright"]): true},
		Text:                  string(regexMap["text"]),
		Files:                 map[string]bool{file: true},
		LicenseAppliesToFiles: map[string]bool{},
		Used:                  false,
	}
	return m
}

// If we find multiple instances of the same license text during our search, deduplicate the match objects by merging them together.
func (m *Match) merge(other *Match) {
	m.Lock()
	for k, v := range other.Copyrights {
		m.Copyrights[k] = v
	}
	for k, v := range other.Projects {
		m.Projects[k] = v
	}
	for k, v := range other.Files {
		m.Files[k] = v
	}
	for k, v := range other.LicenseAppliesToFiles {
		m.LicenseAppliesToFiles[k] = v
	}
	m.Used = m.Used || other.Used
	m.Unlock()
}
