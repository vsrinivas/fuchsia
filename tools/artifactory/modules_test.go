// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package artifactory

import (
	"path/filepath"
	"reflect"
	"testing"
)

// Implements apiModules
type mockAPIModules struct {
	apis []string
}

func (m mockAPIModules) BuildDir() string {
	return "BUILD_DIR"
}

func (m mockAPIModules) APIs() []string {
	return m.apis
}

func TestAPIUploads(t *testing.T) {
	m := &mockAPIModules{
		apis: []string{"a", "b", "c"},
	}
	expected := []Upload{
		{
			Source:      filepath.Join("BUILD_DIR", "a.json"),
			Destination: "namespace/a.json",
		},
		{
			Source:      filepath.Join("BUILD_DIR", "b.json"),
			Destination: "namespace/b.json",
		},
		{
			Source:      filepath.Join("BUILD_DIR", "c.json"),
			Destination: "namespace/c.json",
		},
	}
	actual := buildAPIModuleUploads(m, "namespace")
	if !reflect.DeepEqual(actual, expected) {
		t.Fatalf("unexpected build API uploads:\nexpected: %v\nactual: %v\n", expected, actual)
	}
}
