// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

import (
	"os"

	"go.fuchsia.dev/fuchsia/tools/lib/jsonutil"
)

// TestList contains the list of tests in the build along with
// arbitrary test metadata.
type TestList struct {
	Tests []TestListEntry `json:"tests,omitempty"`
}

// TestListEntry is an entry in the test-list.json.
// See //src/lib/testing/test_list/src/lib.rs.
type TestListEntry struct {
	// Name is the name of the test.
	// MUST BE unique within the test list file.
	Name string `json:"name,omitempty"`

	// Arbitrary labels for this test case.
	Labels []string `json:"labels,omitempty"`

	// Arbitrary tags for this test case.
	Tags []TestTag `json:"tags,omitempty"`
}

// TestTag represents arbitrary test metadata.
type TestTag struct {
	Key   string `json:"key"`
	Value string `json:"value"`
}

// LoadTestList loads test-list.json and returns a map of test names to
// testListEntries.
func LoadTestList(testListPath string) (map[string]TestListEntry, error) {
	var testList TestList
	if err := jsonutil.ReadFromFile(testListPath, &testList); err != nil {
		if os.IsNotExist(err) {
			return nil, nil
		}
		return nil, err
	}

	m := make(map[string]TestListEntry)
	for _, entry := range testList.Tests {
		m[entry.Name] = entry
	}
	return m, nil
}
