// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

import (
	"fmt"
	"os"

	"go.fuchsia.dev/fuchsia/tools/lib/jsonutil"
)

// TestList contains the list of tests in the build along with
// arbitrary test metadata.
type TestList struct {
	Data     []TestListEntry `json:"data,omitempty"`
	SchemaID string          `json:"schema_id"`
}

const TestListSchemaIDExperimental string = "experimental"

// TestListEntry is an entry in the test-list.json.
// See //src/lib/testing/test_list/src/lib.rs.
type TestListEntry struct {
	// Name is the name of the test.
	// MUST BE unique within the test list file.
	Name string `json:"name,omitempty"`

	// Arbitrary labels for this test case.
	Labels []string `json:"labels"`

	// Arbitrary tags for this test case.
	Tags []TestTag `json:"tags,omitempty"`

	// Description of how to execute this test.
	Execution ExecutionDef `json:"execution,omitempty"`
}

type ExecutionDef struct {
	Type            string `json:"type"`
	ComponentURL    string `json:"component_url"`
	TimeoutSeconds  int    `json:"timeout_seconds,omitempty"`
	Parallel        uint16 `json:"parallel,omitempty"`
	MaxSeverityLogs string `json:"max_severity_logs,omitempty"`
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

	if testList.SchemaID != TestListSchemaIDExperimental {
		return nil, fmt.Errorf(`"schema_id" must be %q, found %q`, TestListSchemaIDExperimental, testList.SchemaID)
	}

	m := make(map[string]TestListEntry)
	for _, entry := range testList.Data {
		m[entry.Name] = entry
	}
	return m, nil
}
