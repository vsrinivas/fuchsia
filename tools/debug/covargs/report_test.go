// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package covargs

import (
	"fmt"
	"reflect"
	"strconv"
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

func TestConversion(t *testing.T) {
	var testExport = &llvm.Export{
		Data: []llvm.Data{
			{
				Files: []llvm.File{
					{
						Filename: "/path/to/fuchsia/src/test.cc",
						Segments: []llvm.Segment{
							{1, 16, 20, true, true, false},
							{1, 17, 20, true, true, false},
							{1, 20, 20, true, false, false},
							{1, 24, 2, true, true, false},
							{1, 27, 20, true, false, false},
							{1, 28, 0, false, false, false},
							{2, 37, 2, true, true, false},
							{3, 24, 22, true, true, false},
							{3, 30, 2, true, false, false},
							{3, 32, 20, true, true, false},
							{3, 35, 2, true, false, false},
							{3, 36, 20, true, false, false},
							{3, 37, 20, true, true, false},
							{3, 39, 20, true, true, false},
							{3, 42, 20, true, false, false},
							{3, 48, 2, true, false, false},
							{4, 7, 2, true, true, false},
							{4, 12, 2, true, false, false},
							{4, 13, 0, true, false, true},
							{5, 5, 0, true, true, false},
							{5, 26, 0, true, true, false},
							{5, 32, 0, true, false, false},
							{5, 34, 0, true, true, false},
							{5, 37, 0, true, false, false},
							{5, 39, 0, true, true, false},
							{5, 41, 0, true, true, false},
							{5, 44, 0, true, false, false},
							{5, 50, 2, true, false, false},
							{6, 2, 0, false, false, false},
							{7, 12, 1, true, true, false},
							{11, 2, 0, false, false, false},
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

	var testFiles = []*codecoverage.File{
		{
			Path: "//src/test.cc",
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
	}

	// We pass an empty diff mapping to avoid invoking Git.
	files, err := ConvertFiles(testExport, "/path/to/fuchsia", &DiffMapping{})
	if err != nil {
		t.Fatal(err)
	}

	if !reflect.DeepEqual(files, testFiles) {
		t.Error("expected", testFiles, "but got", files)
	}
}

func TestSummary(t *testing.T) {
	var testFiles = []*codecoverage.File{
		{
			Path: "//src/test1.cc",
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
		{
			Path: "//src/test2.cc",
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
	}

	var testDirs = []*codecoverage.GroupCoverageSummary{
		{
			Path: "//",
			Dirs: []*codecoverage.CoverageSummary{
				{
					Name: "src/",
					Path: "//src/",
					Summaries: []*codecoverage.Metric{
						{
							Name:    "function",
							Covered: int32(4),
							Total:   int32(4),
						},
						{
							Name:    "region",
							Covered: int32(18),
							Total:   int32(32),
						},
						{
							Name:    "line",
							Covered: int32(18),
							Total:   int32(20),
						},
					},
				},
			},
			Summaries: []*codecoverage.Metric{
				{
					Name:    "function",
					Covered: int32(4),
					Total:   int32(4),
				},
				{
					Name:    "region",
					Covered: int32(18),
					Total:   int32(32),
				},
				{
					Name:    "line",
					Covered: int32(18),
					Total:   int32(20),
				},
			},
		},
		{
			Path: "//src/",
			Files: []*codecoverage.CoverageSummary{
				{
					Name: "test1.cc",
					Path: "//src/test1.cc",
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
				{
					Name: "test2.cc",
					Path: "//src/test2.cc",
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
			Summaries: []*codecoverage.Metric{
				{
					Name:    "function",
					Covered: int32(4),
					Total:   int32(4),
				},
				{
					Name:    "region",
					Covered: int32(18),
					Total:   int32(32),
				},
				{
					Name:    "line",
					Covered: int32(18),
					Total:   int32(20),
				},
			},
		},
	}

	var testSummaries = []*codecoverage.Metric{
		{
			Name:    "function",
			Covered: int32(4),
			Total:   int32(4),
		},
		{
			Name:    "region",
			Covered: int32(18),
			Total:   int32(32),
		},
		{
			Name:    "line",
			Covered: int32(18),
			Total:   int32(20),
		},
	}

	dirs, summaries := ComputeSummaries(testFiles)

	if !reflect.DeepEqual(dirs, testDirs) {
		t.Error("expected", testDirs, "but got", dirs)
	}
	if !reflect.DeepEqual(summaries, testSummaries) {
		t.Error("expected", testSummaries, "but got", summaries)
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

	for i, tt := range tests {
		t.Run(strconv.Itoa(i), func(t *testing.T) {
			testDir := t.TempDir()
			var files []*codecoverage.File
			for i := 0; i < tt.numFiles; i++ {
				files = append(files, &codecoverage.File{
					Path:            fmt.Sprintf("//test%d.cc", i+1),
					Lines:           []*codecoverage.LineRange{},
					UncoveredBlocks: []*codecoverage.ColumnRanges{},
					Summaries: []*codecoverage.Metric{
						{
							Name:    "function",
							Covered: 0,
							Total:   0,
						},
						{
							Name:    "region",
							Covered: 0,
							Total:   0,
						},
						{
							Name:    "line",
							Covered: 0,
							Total:   0,
						},
					},
				})
			}

			report, err := SaveReport(files, tt.shardSize, testDir)
			if err != nil {
				t.Error("unexpected error", err)
			}

			if tt.numShards > 0 {
				if numShards := len(report.FileShards); numShards != tt.numShards {
					t.Error("expected", tt.numShards, "but got", numShards)
				}
				if !reflect.DeepEqual(report.FileShards, tt.fileShards) {
					t.Error("expected", tt.fileShards, "but got", report.FileShards)
				}
			} else {
				if numFiles := len(report.Files); numFiles != tt.numFiles {
					t.Error("expected", tt.numFiles, "but got", numFiles)
				}
			}
		})
	}
}
