// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"encoding/json"
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
	"strconv"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"go.fuchsia.dev/fuchsia/tools/testing/runtests"
)

func TestSplitVersion(t *testing.T) {
	for _, tc := range []struct {
		name            string
		arg             string
		expectedVersion string
		expectedPath    string
	}{
		{
			name:            "arg with key",
			arg:             "llvm_profdata=key",
			expectedVersion: "key",
			expectedPath:    "llvm_profdata",
		}, {
			name:            "arg without key",
			arg:             "llvm_profdata",
			expectedVersion: "",
			expectedPath:    "llvm_profdata",
		},
	} {
		t.Run(tc.name, func(t *testing.T) {
			version, path := splitVersion(tc.arg)
			if version != tc.expectedVersion {
				t.Errorf("got version %s, want %s", version, tc.expectedVersion)
			}
			if path != tc.expectedPath {
				t.Errorf("got path %s, want %s", path, tc.expectedPath)
			}
		})
	}
}

func TestReadSummary(t *testing.T) {
	tempDir := t.TempDir()
	var summaryFiles []string
	for i := 0; i < 3; i++ {
		summaryBytes, err := json.Marshal(runtests.TestSummary{
			Tests: []runtests.TestDetails{
				{
					Name:      fmt.Sprintf("foo%d", i),
					Result:    runtests.TestSuccess,
					DataSinks: runtests.DataSinkMap{"llvm-profile": []runtests.DataSink{{Name: fmt.Sprintf("profile%d", i), File: fmt.Sprintf("llvm-profile/profile%d", i)}}},
				},
			},
		})
		if err != nil {
			t.Fatalf("failed to marshal summary: %s", err)
		}
		summaryFile := filepath.Join(tempDir, fmt.Sprintf("summary%d.json", i))
		if err := ioutil.WriteFile(summaryFile, summaryBytes, os.ModePerm); err != nil {
			t.Fatalf("failed to write summary file: %s", err)
		}
		if i > 0 {
			summaryFile += "=version"
		}
		summaryFiles = append(summaryFiles, summaryFile)
	}

	expected := map[string]runtests.DataSinkMap{
		"": {"llvm-profile": []runtests.DataSink{
			{Name: "profile0", File: filepath.Join(tempDir, "llvm-profile/profile0")},
		},
		},
		"version": {"llvm-profile": []runtests.DataSink{
			{Name: "profile1", File: filepath.Join(tempDir, "llvm-profile/profile1")},
			{Name: "profile2", File: filepath.Join(tempDir, "llvm-profile/profile2")},
		},
		},
	}

	actual, err := readSummary(summaryFiles)
	if err != nil {
		t.Errorf("failed to read summaries: %s", err)
	}

	if diff := cmp.Diff(actual, expected); diff != "" {
		t.Errorf("Unexpected sinks (-got +want):\n%s", diff)
	}
}

const validModule = "1696251c"

// getLLVMProfdata returns the path to a mock tool which prints the provided output.
func getLLVMProfdata(t *testing.T, filepath string, output string, wantErr bool) string {
	if wantErr {
		if err := ioutil.WriteFile(filepath, []byte("#!/bin/bash\nexit 1"), os.ModePerm); err != nil {
			t.Fatalf("failed to write mock llvm-profdata tool: %s", err)
		}
	} else {
		if err := ioutil.WriteFile(filepath, []byte(fmt.Sprintf("#!/bin/bash\necho \"%s\"", output)), os.ModePerm); err != nil {
			t.Fatalf("failed to write mock llvm-profdata tool: %s", err)
		}
	}
	return filepath
}

type mockVersionFetcher struct {
}

// getVersion returns the number at the end of the file name as the version.
func (m *mockVersionFetcher) getVersion(file string) (uint64, error) {
	return strconv.ParseUint(string(file[len(file)-1]), 10, 64)
}

func TestMergeEntries(t *testing.T) {
	ctx := context.Background()
	tempDir := t.TempDir()
	malformedProfdata := getLLVMProfdata(t, filepath.Join(tempDir, "malformed"), "", true)
	validProfdata := getLLVMProfdata(t, filepath.Join(tempDir, "valid"), fmt.Sprintf("Binary IDs:\n%s\n", validModule), false)
	missingProfdata := getLLVMProfdata(t, filepath.Join(tempDir, "missing"), "Binary IDs:\n", false)
	cases := []struct {
		name            string
		partitions      map[string]*partition
		sinks           map[string][]runtests.DataSink
		expectedEntries []profileEntry
		expectedSinks   map[string][]string
		wantErr         bool
	}{
		{
			name:       "valid",
			partitions: map[string]*partition{"": {tool: validProfdata}, "version1": {tool: malformedProfdata}, "version2": {tool: validProfdata}},
			sinks:      map[string][]runtests.DataSink{"version2": {{File: "sink1"}, {File: "sink2"}}, "": {{File: "sink0"}}},
			expectedEntries: []profileEntry{
				{Profile: "sink1", Module: validModule},
				{Profile: "sink2", Module: validModule},
				{Profile: "sink0", Module: validModule},
			},
			expectedSinks: map[string][]string{"": {"sink0"}, "version2": {"sink1", "sink2"}},
		},
		{
			name:       "fail if missing llvm-profdata version",
			partitions: map[string]*partition{"": {tool: validProfdata}, "version1": {tool: malformedProfdata}, "version2": {tool: validProfdata}},
			sinks:      map[string][]runtests.DataSink{"version2": {{File: "sink1"}, {File: "sink2"}}, "version3": {{File: "sink0"}}},
			wantErr:    true,
		},
		{
			name:       "missing buildid returns error",
			partitions: map[string]*partition{"": {tool: validProfdata}, "version1": {tool: missingProfdata}, "version2": {tool: validProfdata}},
			sinks:      map[string][]runtests.DataSink{"version1": {{File: "sink1"}}},
			wantErr:    true,
		},
		{
			name:       "malformed profile returns error",
			partitions: map[string]*partition{"": {tool: malformedProfdata}, "version1": {tool: missingProfdata}, "version2": {tool: validProfdata}},
			sinks:      map[string][]runtests.DataSink{"": {{File: "sink1"}}, "version2": {{File: "sink2"}}},
			wantErr:    true,
		},
		{
			name:            "use version fetcher if not specifying summary version",
			partitions:      map[string]*partition{"": {tool: validProfdata}, "5": {tool: malformedProfdata}, "7": {tool: validProfdata}},
			sinks:           map[string][]runtests.DataSink{"": {{File: "sink1"}, {File: "sink7"}}},
			expectedEntries: []profileEntry{{Profile: "sink1", Module: validModule}, {Profile: "sink7", Module: validModule}},
			expectedSinks:   map[string][]string{"": {"sink1"}, "7": {"sink7"}},
		},
		{
			name:       "use default partition with version fetcher",
			partitions: map[string]*partition{"": {tool: validProfdata}, "5": {tool: malformedProfdata}, "7": {tool: validProfdata}},
			sinks:      map[string][]runtests.DataSink{"": {{File: "sink1"}, {File: "sink2"}, {File: "sink7"}}},
			expectedEntries: []profileEntry{
				{Profile: "sink1", Module: validModule},
				{Profile: "sink2", Module: validModule},
				{Profile: "sink7", Module: validModule},
			},
			expectedSinks: map[string][]string{"": {"sink1", "sink2"}, "7": {"sink7"}},
		},
	}
	for _, tc := range cases {
		tc := tc
		t.Run(tc.name, func(t *testing.T) {
			versionedSummaries := make(map[string]runtests.DataSinkMap)
			for version, sinks := range tc.sinks {
				versionedSummaries[version] = runtests.DataSinkMap{llvmProfileSinkType: sinks}
			}
			entries, err := mergeEntries(ctx, &mockVersionFetcher{}, versionedSummaries, tc.partitions)
			if tc.wantErr != (err != nil) {
				t.Errorf("got err: %s, want err: %v", err, tc.wantErr)
			}
			sortSlicesOpt := cmpopts.SortSlices(func(a, b profileEntry) bool { return a.Profile < b.Profile })
			if diff := cmp.Diff(entries, tc.expectedEntries, sortSlicesOpt); diff != "" {
				t.Errorf("unexpected entries: (-got +want):\n%s", diff)
			}

			if !tc.wantErr {
				sortSlicesOpt = cmpopts.SortSlices(func(a, b string) bool { return a < b })
				for version, partition := range tc.partitions {
					if diff := cmp.Diff(partition.profiles, tc.expectedSinks[version], sortSlicesOpt); diff != "" {
						t.Errorf("unexpected profiles for version %q: (-got +want):\n%s", version, diff)
					}
				}
			}
		})
	}
}
