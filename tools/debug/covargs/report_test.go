// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package covargs

import (
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
	"reflect"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/debug/covargs/api/llvm"
	"go.fuchsia.dev/fuchsia/tools/debug/covargs/api/third_party/codecoverage"
)

// The data below were collected from the following program which is the
// canonical examples given on the Clang Source-based Code Coverage page [1]:
//
//   #define BAR(x) ((x) || (x))
//   template <typename T> void foo(T x) {
//     for (unsigned I = 0; I < 10; ++I) { BAR(I); }
//     if (false)
//       for (unsigned J = 0; J < 10; ++J) { BAR(J); }
//   }
//   int main() {
//     foo<int>(0);
//     foo<float>(0);
//     return 0;
//   }
//
// [1] https://clang.llvm.org/docs/SourceBasedCodeCoverage.html

var testExport = &llvm.Export{
	Data: []llvm.Data{
		{
			Files: []llvm.File{
				{
					Filename: "/path/to/fuchsia/tools/debug/covargs/test/foo.cc",
					Segments: []llvm.Segment{
						{1, 16, 20, true, true},
						{1, 17, 20, true, true},
						{1, 20, 20, true, false},
						{1, 24, 2, true, true},
						{1, 27, 20, true, false},
						{1, 28, 0, false, false},
						{2, 37, 2, true, true},
						{3, 24, 22, true, true},
						{3, 30, 2, true, false},
						{3, 32, 20, true, true},
						{3, 35, 2, true, false},
						{3, 36, 20, true, false},
						{3, 37, 20, true, true},
						{3, 39, 20, true, true},
						{3, 42, 20, true, false},
						{3, 48, 2, true, false},
						{4, 7, 2, true, true},
						{4, 12, 2, true, false},
						{4, 13, 0, true, false},
						{5, 5, 0, true, true},
						{5, 26, 0, true, true},
						{5, 32, 0, true, false},
						{5, 34, 0, true, true},
						{5, 37, 0, true, false},
						{5, 39, 0, true, true},
						{5, 41, 0, true, true},
						{5, 44, 0, true, false},
						{5, 50, 2, true, false},
						{6, 2, 0, false, false},
						{7, 12, 1, true, true},
						{11, 2, 0, false, false},
					},
					Summary: llvm.Summary{
						Functions: llvm.Counts{
							Count:   2,
							Covered: 2,
							Percent: 100,
						},
						Instantiations: llvm.Counts{
							Count:   3,
							Covered: 3,
							Percent: 100,
						},
						Lines: llvm.Counts{
							Count:   10,
							Covered: 9,
							Percent: 90,
						},
						Regions: llvm.Counts{
							Count:      16,
							Covered:    9,
							NotCovered: 7,
							Percent:    56.25,
						},
					},
				},
			},
			Totals: llvm.Summary{
				Functions: llvm.Counts{
					Count:   2,
					Covered: 2,
					Percent: 100,
				},
				Instantiations: llvm.Counts{
					Count:   3,
					Covered: 3,
					Percent: 100,
				},
				Lines: llvm.Counts{
					Count:   10,
					Covered: 9,
					Percent: 90,
				},
				Regions: llvm.Counts{
					Count:      16,
					Covered:    9,
					NotCovered: 7,
					Percent:    56.25,
				},
			},
		},
	},
	Type:    "llvm.coverage.json.export",
	Version: "2.0.0",
}

var testReport = &codecoverage.CoverageReport{
	Files: []*codecoverage.File{
		{
			Path: "//tools/debug/covargs/test/foo.cc",
			Lines: []*codecoverage.LineRange{
				{
					First: int32(1),
					Last:  int32(1),
					Count: int64(20),
				},
				{
					First: int32(2),
					Last:  int32(2),
					Count: int64(2),
				},
				{
					First: int32(3),
					Last:  int32(3),
					Count: int64(22),
				},
				{
					First: int32(4),
					Last:  int32(4),
					Count: int64(2),
				},
				{
					First: int32(5),
					Last:  int32(5),
				},
				{
					First: int32(6),
					Last:  int32(6),
					Count: int64(2),
				},
				{
					First: int32(7),
					Last:  int32(11),
					Count: int64(1),
				},
			},
			UncoveredBlocks: []*codecoverage.ColumnRanges{
				{
					Line: int32(4),
					Ranges: []*codecoverage.ColumnRange{
						{
							First: int32(13),
							Last:  int32(-1),
						},
					},
				},
			},
			Summaries: []*codecoverage.Metric{
				{
					Name:    "function",
					Covered: int32(2),
					Total:   int32(2),
				},
				{
					Name:    "region",
					Covered: int32(9),
					Total:   int32(16),
				},
				{
					Name:    "line",
					Covered: int32(9),
					Total:   int32(10),
				},
			},
		},
	},
}

func TestGenerate(t *testing.T) {
	report, err := GenerateReport(testExport, "/path/to/fuchsia", nil)
	if err != nil {
		t.Fatal(err)
	}

	if !reflect.DeepEqual(report, testReport) {
		t.Error("expected", testReport, "but got", report)
	}
}

func TestSave(t *testing.T) {
	tests := []struct {
		numFiles   int
		shardSize  int
		numShards  int
		fileShards []string
	}{
		{1, 1, 0, nil},
		{3, 3, 0, nil},
		{6, 3, 2, []string{"files1.json.gz", "files2.json.gz"}},
		{8, 3, 3, []string{"files1.json.gz", "files2.json.gz", "files3.json.gz"}},
	}

	dir, err := ioutil.TempDir("", "covargs")
	if err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(dir)

	for i, tt := range tests {
		testDir := filepath.Join(dir, fmt.Sprintf("test%d", i))
		err := os.MkdirAll(testDir, os.ModePerm)
		if err != nil {
			t.Fatal(err)
		}

		report := &codecoverage.CoverageReport{}
		for i := 0; i < tt.numFiles; i++ {
			report.Files = append(report.Files, &codecoverage.File{})
		}

		report, err = SaveReport(report, tt.shardSize, testDir)
		if err != nil {
			t.Error("unexpected error", err)
		}

		if numFiles := len(report.Files); numFiles != tt.numFiles {
			t.Error("expected", tt.numFiles, "but got", numFiles)
		}
		if numShards := len(report.FileShards); numShards != tt.numShards {
			t.Error("expected", tt.numShards, "but got", numShards)
		}
		if !reflect.DeepEqual(report.FileShards, tt.fileShards) {
			t.Error("expected", tt.fileShards, "but got", report.FileShards)
		}
	}
}
