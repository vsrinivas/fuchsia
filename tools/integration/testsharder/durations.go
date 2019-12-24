// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testsharder

import (
	"go.fuchsia.dev/fuchsia/tools/build/lib"
)

// Exactly one entry in the durations file will have this as its test_name field. The
// durations specified by this entry will be applied to any tests that aren't
// found in the durations file (e.g. because they have recently been added or
// renamed).
var defaultDurationKey = "*"

// TestDurationsMap maps test names to corresponding test duration data.
type TestDurationsMap map[string]build.TestDuration

func NewTestDurationsMap(durations []build.TestDuration) TestDurationsMap {
	durationsMap := TestDurationsMap{}
	for _, d := range durations {
		durationsMap[d.Name] = d
	}
	return durationsMap
}

// Get returns the duration data for a given test. If the test is not included
// in the durations map, the default duration data is returned instead.
func (m TestDurationsMap) Get(test build.Test) build.TestDuration {
	// Look up the test by the unique name that is used in summary.json.
	key := test.Path
	if test.OS == "fuchsia" {
		key = test.PackageURL
	}

	if testData, ok := m[key]; ok {
		return testData
	}
	return m[defaultDurationKey]
}
