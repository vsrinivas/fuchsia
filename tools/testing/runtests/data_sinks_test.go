// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package runtests

import (
	"path"
	"path/filepath"
	"reflect"
	"testing"
)

type fakeViewer struct {
	summaryJSON TestSummary
	copiedFiles map[string]string
}

func (v fakeViewer) summary(_ string) (*TestSummary, error) {
	return &v.summaryJSON, nil
}

func (v fakeViewer) copyFile(remote, local string) error {
	v.copiedFiles[local] = remote
	return nil
}

func (v fakeViewer) close() error {
	return nil
}

func TestCopyDataSinks(t *testing.T) {
	remoteOutputDir := "/remote/output/dir"
	localOutputDir := "/local/output/dir"

	expectedSinks := DataSinkMap{
		"sink": {
			{
				Name: "sink1",
				File: filepath.Join(localOutputDir, "a/b/c/sink1"),
			},
			{
				Name: "sink2",
				File: filepath.Join(localOutputDir, "a/d/e/sink2"),
			},
		},
	}
	summarySinks := DataSinkMap{
		"sink": {
			{
				Name: "sink1",
				File: "a/b/c/sink1",
			},
			{
				Name: "sink2",
				File: "a/d/e/sink2",
			},
		},
	}
	summary := TestSummary{
		Tests: []TestDetails{
			{
				Name:      "foo_test",
				DataSinks: summarySinks,
			},
		},
	}

	viewer := fakeViewer{summaryJSON: summary, copiedFiles: map[string]string{}}
	dataSinks, err := DataSinkCopier{viewer}.Copy(remoteOutputDir, localOutputDir)
	if err != nil {
		t.Fatalf("failed to copy data sinks: %s", err)
	}

	if !reflect.DeepEqual(dataSinks, expectedSinks) {
		t.Errorf("got data sinks %v, expected %v", dataSinks, expectedSinks)
	}

	for i, sink := range dataSinks["sink"] {
		expectedRemoteSource := path.Join(remoteOutputDir, summarySinks["sink"][i].File)
		actualRemoteSource, ok := viewer.copiedFiles[sink.File]
		if !ok {
			t.Errorf("sink file %s was not copied", sink.File)
		}
		if actualRemoteSource != expectedRemoteSource {
			t.Errorf("sink file %s was copied from the wrong source (got %s, expected %s)",
				sink.File, actualRemoteSource, expectedRemoteSource)
		}
	}
}
