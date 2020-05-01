// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"encoding/json"
	"io/ioutil"
	"os"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/testing/runtests"

	"github.com/google/go-cmp/cmp"
)

func TestLoadTestSummaryPassesInputSummaryThrough(t *testing.T) {
	summaryFile, err := ioutil.TempFile("", "summary.json")
	if err != nil {
		t.Fatal("TempFile() failed:", err)
	}
	defer func() {
		if err := os.Remove(summaryFile.Name()); err != nil {
			t.Errorf("os.Remove(%s) failed: %v", summaryFile.Name(), err)
		}
	}()

	inputSummary := runtests.TestSummary{
		Tests: []runtests.TestDetails{
			{Name: "test name"},
		},
	}
	summaryBytes, err := json.Marshal(inputSummary)
	if err != nil {
		t.Fatal("Marshal(inputSummary) failed:", err)
	}
	if _, err := summaryFile.Write(summaryBytes); err != nil {
		t.Fatal("summaryFile.Write() failed:", err)
	}

	outputSummary, err := loadTestSummary(summaryFile.Name())
	if err != nil {
		t.Errorf("loadSwarmingTaskSummary failed: %v", err)
	} else if diff := cmp.Diff(outputSummary, &inputSummary); diff != "" {
		t.Errorf("loadSwarmingTaskSummary reutrned wrong value (-got +want):\n%s", diff)
	}

	if err := summaryFile.Close(); err != nil {
		t.Fatal(err)
	}
}
