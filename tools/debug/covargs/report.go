// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package covargs

import (
	"compress/zlib"
	"fmt"
	"math"
	"os"
	"path/filepath"
	"sort"

	"github.com/golang/protobuf/jsonpb"
	"go.fuchsia.dev/fuchsia/tools/debug/covargs/api/llvm"
	"go.fuchsia.dev/fuchsia/tools/debug/covargs/api/third_party/codecoverage"
)

// DiffMapping represents a source transformation done by a diff (i.e. patch),
// where for each file it has a mapping from the old line number to the new one.
type DiffMapping map[string]LineMapping

// LineMapping maps the old line number to the new one within a single file.
type LineMapping map[int]int

type line struct {
	line, count int
}
type lineData []line
type block struct {
	first, last int
}
type blockData map[int][]block

func extractData(segments []llvm.Segment) (lineData, blockData) {
	var ld lineData
	bd := blockData{}

	// The most recent segment that starts from a previous line.
	var w *llvm.Segment

	var lineSegments []llvm.Segment
	nextSegmentIndex := 0

	for i := 1; i <= segments[len(segments)-1].LineNumber; i += 1 {
		// Calculate the execution count for each line. Follow the logic in llvm-cov:
		// https://github.com/llvm-mirror/llvm/blob/3b35e17b21e388832d7b560a06a4f9eeaeb35330/lib/ProfileData/Coverage/CoverageMapping.cpp#L686
		if len(lineSegments) > 0 {
			w = &lineSegments[len(lineSegments)-1]
		}

		lineSegments = nil
		for nextSegmentIndex < len(segments) && segments[nextSegmentIndex].LineNumber == i {
			lineSegments = append(lineSegments, segments[nextSegmentIndex])
			nextSegmentIndex += 1
		}

		lineStartsNewRegion := false
		for _, s := range lineSegments {
			if s.HasCount && s.IsRegionEntry {
				lineStartsNewRegion = true
				break
			}
		}
		startOfSkippedRegion := (len(lineSegments) > 0 && !lineSegments[0].HasCount) && lineSegments[0].IsRegionEntry
		coverable := !startOfSkippedRegion && ((w != nil && w.HasCount) || lineStartsNewRegion)
		if !coverable {
			continue
		}

		count := 0
		if w != nil {
			count = w.Count
		}

		for _, s := range lineSegments {
			if s.HasCount && s.IsRegionEntry {
				if s.Count > count {
					count = s.Count
				}
			}
		}

		ld = append(ld, line{i, count})

		// Calculate the uncovered blocks within the current line. Follow the logic in llvm-cov:
		// https://github.com/llvm-mirror/llvm/blob/993ef0ca960f8ffd107c33bfbf1fd603bcf5c66c/tools/llvm-cov/SourceCoverageViewText.cpp#L114
		if count == 0 {
			// Skips calculating uncovered blocks if the whole line is not covered.
			continue
		}

		columnStart := 1
		isBlockNotCovered := (w != nil && w.HasCount) && w.Count == 0
		for _, s := range lineSegments {
			columnEnd := s.ColumnNumber
			if isBlockNotCovered {
				bd[s.LineNumber] = append(bd[s.LineNumber], block{columnStart, columnEnd - 1})
			}
			isBlockNotCovered = s.HasCount && s.Count == 0
			columnStart = columnEnd
		}
		if isBlockNotCovered {
			lastSegment := lineSegments[len(lineSegments)-1]
			bd[lastSegment.LineNumber] = append(bd[lastSegment.LineNumber], block{columnStart, -1})
		}
	}

	return ld, bd
}

func rebaseData(lines lineData, blocks blockData, mapping LineMapping) (lineData, blockData) {
	var ld lineData
	for _, l := range lines {
		if _, ok := mapping[l.line]; !ok {
			continue
		}
		ld = append(ld, line{mapping[l.line], l.count})
	}

	var bd blockData
	for i, b := range blocks {
		if _, ok := mapping[i]; !ok {
			continue
		}
		bd[mapping[i]] = b
	}

	return ld, bd
}

func compressData(lines lineData, blocks blockData) ([]*codecoverage.LineRange, []*codecoverage.ColumnRanges) {
	// Aggregate contiguous blocks of lines with the exact same hit count.
	var lr []*codecoverage.LineRange
	last := 0
	for i := 1; i <= len(lines); i++ {
		isContinuous := i < len(lines) && lines[i].line == lines[i-1].line+1
		hasSameCount := i < len(lines) && lines[i].count == lines[i-1].count
		// Merge two lines iff they have continuous line number and exactly the same count, e.g. (101, 10) and (102, 10).
		if isContinuous && hasSameCount {
			continue
		}
		lr = append(lr, &codecoverage.LineRange{
			First: int32(lines[last].line),
			Last:  int32(lines[i-1].line),
			Count: int64(lines[last].count),
		})
		last = i
	}

	var cr []*codecoverage.ColumnRanges
	for i, block := range blocks {
		var ranges []*codecoverage.ColumnRange
		for _, b := range block {
			ranges = append(ranges, &codecoverage.ColumnRange{
				First: int32(b.first),
				Last:  int32(b.last),
			})
		}
		cr = append(cr, &codecoverage.ColumnRanges{
			Line:   int32(i),
			Ranges: ranges,
		})
	}

	return lr, cr
}

func convertFile(file llvm.File, base string, mapping *DiffMapping) (*codecoverage.File, error) {
	if file.Segments == nil {
		return nil, nil
	}

	rel, err := filepath.Rel(base, file.Filename)
	if err != nil {
		return nil, err
	}

	ld, bd := extractData(file.Segments)
	sort.Slice(ld, func(i, j int) bool {
		return ld[i].line < ld[j].line
	})

	if mapping != nil {
		if _, ok := (*mapping)[rel]; ok {
			ld, bd = rebaseData(ld, bd, (*mapping)[rel])
		}
	}

	lr, cr := compressData(ld, bd)

	return &codecoverage.File{
		Path:            "//" + rel,
		Lines:           lr,
		UncoveredBlocks: cr,
		Summaries: []*codecoverage.Metric{
			{
				Name:    "function",
				Covered: int32(file.Summary.Functions.Covered),
				Total:   int32(file.Summary.Functions.Count),
			},
			{
				Name:    "region",
				Covered: int32(file.Summary.Regions.Covered),
				Total:   int32(file.Summary.Regions.Count),
			},
			{
				Name:    "line",
				Covered: int32(file.Summary.Lines.Covered),
				Total:   int32(file.Summary.Lines.Count),
			},
		},
	}, nil
}

// GenerateReport converts the data in LLVM coverage JSON format into the
// compressed coverage format used by Chromium coverage service.
func GenerateReport(export *llvm.Export, base string, mapping *DiffMapping) (*codecoverage.CoverageReport, error) {
	var files []*codecoverage.File
	for _, d := range export.Data {
		// TODO(phosek): Use goroutines to process files in parallel.
		for _, f := range d.Files {
			file, err := convertFile(f, base, mapping)
			if err != nil {
				return nil, err
			}
			files = append(files, file)
		}
	}
	return &codecoverage.CoverageReport{Files: files}, nil
}

func saveReport(report *codecoverage.CoverageReport, filename string) error {
	f, err := os.Create(filename)
	if err != nil {
		return fmt.Errorf("cannot open file %q: %v", filename, err)
	}
	defer f.Close()
	w := zlib.NewWriter(f)
	defer w.Close()
	m := &jsonpb.Marshaler{}
	if err := m.Marshal(w, report); err != nil {
		return fmt.Errorf("cannot marshal report: %w", err)
	}
	return nil
}

// SaveReport saves compresses coverage data to disk, optionally sharding the
// data into multiple files each of the same size.
func SaveReport(report *codecoverage.CoverageReport, shardSize int, dir string) (*codecoverage.CoverageReport, error) {
	numFiles := len(report.Files)
	if numFiles > shardSize {
		numShards := int(math.Ceil(float64(numFiles) / float64(shardSize)))
		fileShards := make([]string, numShards)
		// TODO(phosek): Use goroutines to process slices in parallel.
		for i := 0; i < numShards; i++ {
			from := i * shardSize
			to := (i + 1) * shardSize
			if to > numFiles {
				to = numFiles
			}
			report := codecoverage.CoverageReport{Files: report.Files[from:to]}
			filename := fmt.Sprintf("files%d.json.gz", i)
			if err := saveReport(&report, filepath.Join(dir, filename)); err != nil {
				return nil, fmt.Errorf("failed to save report %q: %w", filename, err)
			}
			fileShards[i] = filename
		}
		report = &codecoverage.CoverageReport{FileShards: fileShards}
	}
	const filename = "all.json.gz"
	if err := saveReport(report, filepath.Join(dir, filename)); err != nil {
		return nil, fmt.Errorf("failed to save report %q: %w", filename, err)
	}
	return report, nil
}
