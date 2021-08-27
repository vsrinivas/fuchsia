// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testsharder

import (
	"strings"

	"go.fuchsia.dev/fuchsia/tools/build"
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
func (m TestDurationsMap) Get(test Test) build.TestDuration {
	if testData, ok := m[test.Test.Name]; ok {
		return testData
	} else if strings.HasSuffix(test.Test.Name, ".cm") {
		// TODO(fxbug.dev/83553): This is a hack to ensure we continue to use
		// existing test duration data for legacy component tests even when the
		// extension changes from ".cmx" to ".cm". Remove it 3 days after the
		// migration happens, at which point test duration files will contain
		// the new names.
		if testData, ok := m[test.Test.Name+"x"]; ok {
			return testData
		}
	}
	return m[defaultDurationKey]
}
