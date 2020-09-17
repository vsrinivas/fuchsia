// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package checklicenses

import (
	"regexp"
	"sync"
)

// License contains a searchable regex pattern for finding file matches in tree. The category field is the .lic name
type License struct {
	pattern  *regexp.Regexp
	matches  map[string]*Match
	category string
}

// Match is used to store a single match result alongside the License along with a list of all matching files
type Match struct {
	// TODO(solomonkinard) value should be byte, not []byte since only one result is stored
	value []byte
	files []string
}

// LicenseFindMatch runs concurrently for all licenses, synchronizing result production for subsequent consumption
func (license *License) LicenseFindMatch(index int, data []byte, sm *sync.Map, wg *sync.WaitGroup) {
	defer wg.Done()
	sm.Store(index, license.pattern.Find(data))
}

func (license *License) append(path string) {
	// TODO(solomonkinard) use first license match (durign single license file check) instead of pattern
	// TODO(solomonkinard) once the above is done, delete the len() check here since it will be impossible
	regAuthor := `(?i)Copyright( ©| \((C)\))? [\d]{4}(\s|,|-|[\d]{4})*(.*)(All rights reserved)?`
	// The capture group for the author name is 4.
	captureGroup := 4
	re := regexp.MustCompile(regAuthor)
	finalAuthors := re.FindStringSubmatch(license.pattern.String())
	authorName := ""
	if len(finalAuthors) >= captureGroup && finalAuthors[captureGroup] != "" {
		authorName = finalAuthors[captureGroup]
	}
	if len(license.matches) == 0 {
		license.matches = make(map[string]*Match)
	}
	_, f := license.matches[authorName]
	if !f {
		license.matches[authorName] = &Match{value: []byte(license.pattern.String())}
	}
	license.matches[authorName].files = append(license.matches[authorName].files, path)
}
