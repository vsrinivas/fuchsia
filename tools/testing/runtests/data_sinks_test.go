// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package runtests

import (
	"path"
	"reflect"
	"testing"
)

type fakeViewer struct {
	s           TestSummary
	allSinks    []string
	copiedFiles map[string]string
}

func (v fakeViewer) summary(_ string) (*TestSummary, error) {
	return &v.s, nil
}

func (v fakeViewer) getAllDataSinks(remote string) ([]string, error) {
	return v.allSinks, nil
}

func (v fakeViewer) copyFile(remote, local string) error {
	v.copiedFiles[remote] = local
	return nil
}

func (v fakeViewer) close() error {
	return nil
}

func TestGetAllDataSinks(t *testing.T) {
	expectedSinks := []DataSink{
		{
			Name: "sink1",
			File: "sink1",
		},
		{
			Name: "sink2",
			File: "sink2",
		},
	}
	viewer := fakeViewer{allSinks: []string{"sink1", "sink2"}, copiedFiles: map[string]string{}}
	copier := DataSinkCopier{viewer: viewer}
	dataSinks, err := copier.GetAllDataSinks("REMOTE_DIR")
	if err != nil {
		t.Fatalf("failed to get data sinks: %s", err)
	} else if !reflect.DeepEqual(dataSinks, expectedSinks) {
		t.Errorf("got data sinks %v, expected %v", dataSinks, expectedSinks)
	}
}

func TestCopyDataSinks(t *testing.T) {
	expectedSinks := map[string]DataSinkReference{
		"foo_test": {
			DataSinkMap{
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
			}, "REMOTE_DIR",
		},
	}
	summary := TestSummary{
		Tests: []TestDetails{
			{
				Name:      "foo_test",
				DataSinks: expectedSinks["foo_test"].Sinks,
			},
		},
	}

	viewer := fakeViewer{s: summary, copiedFiles: map[string]string{}}
	copier := DataSinkCopier{viewer: viewer}
	dataSinks, err := copier.GetReferences("REMOTE_DIR")
	if err != nil {
		t.Fatalf("failed to get data sinks: %s", err)
	} else if !reflect.DeepEqual(dataSinks, expectedSinks) {
		t.Errorf("got data sinks %v, expected %v", dataSinks, expectedSinks)
	}
	var sinkRefs []DataSinkReference
	for _, ref := range dataSinks {
		sinkRefs = append(sinkRefs, ref)
	}
	_, err = copier.Copy(sinkRefs, "LOCAL_DIR")
	if err != nil {
		t.Fatalf("failed to copy data sinks: %s", err)
	}

	for _, ref := range dataSinks {
		for _, sink := range ref.Sinks["sink"] {
			src := path.Join("REMOTE_DIR", sink.File)
			expectedDest := path.Join("LOCAL_DIR", sink.File)
			actualDest, ok := viewer.copiedFiles[src]
			if !ok {
				t.Errorf("sink %q was not copied", sink.File)
			}
			if expectedDest != actualDest {
				t.Errorf("sink %q was copied to the wrong destination:\nexpected: %s\nactual %s",
					sink.File, expectedDest, actualDest)
			}
		}
	}
}
