// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package covargs

import (
	"bytes"
	"compress/zlib"
	"fmt"
	"math"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"sort"
	"strconv"
	"strings"
	"sync"

	"go.fuchsia.dev/fuchsia/tools/debug/covargs/api/llvm"
	"go.fuchsia.dev/fuchsia/tools/debug/covargs/api/third_party/codecoverage"
	"golang.org/x/sync/errgroup"
	"google.golang.org/protobuf/encoding/protojson"
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

func getRevision(path string) (string, int64, error) {
	if _, err := os.Stat(path); os.IsNotExist(err) {
		return "", 0, nil
	}
	var stdout bytes.Buffer
	rel, err := filepath.Rel(filepath.Dir(path), path)
	if err != nil {
		return "", 0, err
	}
	cmd := exec.Command("git", "--literal-pathspecs", "log", "-n", "1", `--pretty=format:%H:%ct`, rel)
	cmd.Dir = filepath.Dir(path)
	cmd.Stdout = &stdout
	if err := cmd.Run(); err != nil {
		return "", 0, fmt.Errorf("failed to obtain revision: %w", err)
	}
	out := strings.TrimSpace(stdout.String())
	if out == "" {
		return "", 0, nil
	}
	parts := strings.Split(out, ":")
	if len(parts) != 2 {
		return "", 0, fmt.Errorf("not in hash:timestamp format: %s", out)
	}
	hash := parts[0]
	timestamp, err := strconv.ParseInt(parts[1], 10, 64)
	if err != nil {
		return "", 0, fmt.Errorf("invalid timestamp %q: %w", parts[1], err)
	}
	return hash, timestamp, nil
}

func convertFile(file llvm.File, base string, mapping *DiffMapping) (*codecoverage.File, error) {
	if file.Segments == nil {
		return nil, nil
	}

	// The filename is expected to be relative to the current working
	// directory. However, in the report, we need to make it relative to the
	// base directory of the source tree.
	abs, err := filepath.Abs(file.Filename)
	if err != nil {
		return nil, err
	}
	rel, err := filepath.Rel(base, abs)
	if err != nil {
		return nil, err
	}

	ld, bd := extractData(file.Segments)
	sort.Slice(ld, func(i, j int) bool {
		return ld[i].line < ld[j].line
	})

	var revision string
	var timestamp int64
	if mapping != nil {
		if _, ok := (*mapping)[rel]; ok {
			ld, bd = rebaseData(ld, bd, (*mapping)[rel])
		}
	} else {
		revision, timestamp, err = getRevision(file.Filename)
		if err != nil {
			return nil, err
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
		Revision:  revision,
		Timestamp: timestamp,
	}, nil
}

func mergeSummaries(summaries ...[]*codecoverage.Metric) []*codecoverage.Metric {
	s := []*codecoverage.Metric{
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
	}
	d := map[string]int{}
	for i, m := range s {
		d[m.Name] = i
	}
	for _, m := range summaries {
		if m != nil {
			for _, n := range m {
				s[d[n.Name]].Covered += n.Covered
				s[d[n.Name]].Total += n.Total
			}
		}
	}
	return s
}

// ComputeSummaries calculates aggregate summaries for all directories.
func ComputeSummaries(files []*codecoverage.File) ([]*codecoverage.GroupCoverageSummary, []*codecoverage.Metric) {
	summaries := map[string][]*codecoverage.Metric{}
	for _, f := range files {
		// We can't use filepath.Dir and filepath.Base because they normalize '//'.
		dir, _ := filepath.Split(f.Path)
		for dir != "//" {
			// In the coverage data format, dirs end with '/' except for root.
			summaries[dir] = mergeSummaries(summaries[dir], f.Summaries)
			dir, _ = filepath.Split(dir[:len(dir)-1])
		}
		summaries["//"] = mergeSummaries(summaries["//"], f.Summaries)
	}

	fileSummaries := map[string][]*codecoverage.CoverageSummary{}
	for _, f := range files {
		dir, file := filepath.Split(f.Path)
		fileSummaries[dir] = append(fileSummaries[dir], &codecoverage.CoverageSummary{
			Name:      file,
			Path:      f.Path,
			Summaries: f.Summaries,
		})
	}

	dirSummaries := map[string][]*codecoverage.CoverageSummary{}
	for p, s := range summaries {
		if p == "//" {
			continue
		}
		// The path already ends with '/' so we have to omit it.
		dir, file := filepath.Split(p[:len(p)-1])
		dirSummaries[dir] = append(dirSummaries[dir], &codecoverage.CoverageSummary{
			Name:      file + "/",
			Path:      p,
			Summaries: s,
		})
	}

	var groupSummaries []*codecoverage.GroupCoverageSummary
	for p, s := range summaries {
		groupSummaries = append(groupSummaries, &codecoverage.GroupCoverageSummary{
			Path:      p,
			Dirs:      dirSummaries[p],
			Files:     fileSummaries[p],
			Summaries: s,
		})
	}
	sort.Slice(groupSummaries, func(i, j int) bool {
		return groupSummaries[i].Path < groupSummaries[j].Path
	})

	return groupSummaries, summaries["//"]
}

// ConvertFiles converts the data in LLVM coverage JSON format into the
// compressed coverage format used by Chromium coverage service.
func ConvertFiles(export *llvm.Export, base string, mapping *DiffMapping) ([]*codecoverage.File, error) {
	var files []*codecoverage.File
	var g errgroup.Group
	var mu sync.Mutex
	s := make(chan struct{}, runtime.NumCPU())
	for _, d := range export.Data {
		for _, f := range d.Files {
			f := f
			s <- struct{}{}
			g.Go(func() error {
				defer func() { <-s }()
				file, err := convertFile(f, base, mapping)
				if err != nil {
					return err
				}
				mu.Lock()
				files = append(files, file)
				mu.Unlock()
				return nil
			})
		}
	}
	if err := g.Wait(); err != nil {
		return nil, err
	}
	return files, nil
}

func saveReport(report *codecoverage.CoverageReport, filename string) error {
	f, err := os.Create(filename)
	if err != nil {
		return fmt.Errorf("cannot open file %q: %v", filename, err)
	}
	defer f.Close()
	w := zlib.NewWriter(f)
	defer w.Close()
	b, err := protojson.MarshalOptions{
		UseProtoNames:   true,
		EmitUnpopulated: true,
	}.Marshal(report)
	if err != nil {
		return fmt.Errorf("cannot marshal report: %w", err)
	}
	if _, err := w.Write(b); err != nil {
		return fmt.Errorf("cannot emit report: %w", err)
	}
	return nil
}

// SaveReport saves compresses coverage data to disk, optionally sharding the
// data into multiple files each of the same size.
func SaveReport(files []*codecoverage.File, shardSize int, dir string) (*codecoverage.CoverageReport, error) {
	dirs, summaries := ComputeSummaries(files)
	report := &codecoverage.CoverageReport{
		Dirs:      dirs,
		Summaries: summaries,
	}
	if numFiles := len(files); numFiles > shardSize {
		const filename = "files%0*d.json.gz"
		numShards := int(math.Ceil(float64(numFiles) / float64(shardSize)))
		width := 1 + int(math.Log10(float64(numShards)))
		fileShards := make([]string, numShards)
		// TODO(phosek): Use goroutines to process slices in parallel.
		for i := 0; i < numShards; i++ {
			from := i * shardSize
			to := (i + 1) * shardSize
			if to > numFiles {
				to = numFiles
			}
			report := codecoverage.CoverageReport{Files: files[from:to]}
			filename := fmt.Sprintf(filename, width, i+1)
			if err := saveReport(&report, filepath.Join(dir, filename)); err != nil {
				return nil, fmt.Errorf("failed to save report %q: %w", filename, err)
			}
			fileShards[i] = filename
		}
		report.FileShards = fileShards
	} else {
		report.Files = files
	}
	const filename = "all.json.gz"
	if err := saveReport(report, filepath.Join(dir, filename)); err != nil {
		return nil, fmt.Errorf("failed to save report %q: %w", filename, err)
	}
	return report, nil
}
