// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package lib

import (
	"regexp"
	"sync"
)

// License contains a searchable regex pattern for finding file matches in tree. The category field is the .lic name
type License struct {
	pattern  *regexp.Regexp
	matches  []Match
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
	matched := license.pattern.Find(data)
	sm.Store(index, matched)
}
