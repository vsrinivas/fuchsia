// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package llvm

import (
	"encoding/json"
	"fmt"
)

type Export struct {
	Data    []Data `json:"data"`
	Type    string `json:"type"`
	Version string `json:"version"`
}

type Data struct {
	Files     []File     `json:"files"`
	Functions []Function `json:"functions,omitempty"`
	Totals    Summary    `json:"totals"`
}

type File struct {
	Expansions []Expansion `json:"expansions,omitempty"`
	Filename   string      `json:"filename"`
	Segments   []Segment   `json:"segments"`
	Summary    Summary     `json:"summary"`
}

type Summary struct {
	Functions      Counts `json:"functions"`
	Instantiations Counts `json:"instantiations"`
	Lines          Counts `json:"lines"`
	Regions        Counts `json:"regions"`
}

type Expansion struct {
	Filenames     []string `json:"filenames"`
	SourceRegion  []int    `json:"source_region"`
	TargetRegions [][]int  `json:"target_regions"`
}

type Segment struct {
	LineNumber    int
	ColumnNumber  int
	Count         int
	HasCount      bool
	IsRegionEntry bool
	IsGapRegion   bool
}

func (s *Segment) UnmarshalJSON(buf []byte) error {
	tmp := []interface{}{&s.LineNumber, &s.ColumnNumber, &s.Count, &s.HasCount, &s.IsRegionEntry, &s.IsGapRegion}
	wantLen := len(tmp)
	if err := json.Unmarshal(buf, &tmp); err != nil {
		return err
	}
	if l, k := len(tmp), wantLen; l != k {
		return fmt.Errorf("wrong number of fields in Segment: %d != %d", l, k)
	}
	return nil
}

type Function struct {
	Count     int      `json:"count"`
	Filenames []string `json:"filenames"`
	Name      string   `json:"name"`
	Regions   []Region `json:"regions"`
}

type Region struct {
	LineStart      int
	ColumnStart    int
	LineEnd        int
	ColumnEnd      int
	ExecutionCount int
	FileID         int
	ExpandedFileID int
	Kind           int
}

func (r *Region) UnmarshalJSON(buf []byte) error {
	tmp := []interface{}{&r.LineStart, &r.ColumnStart, &r.LineEnd, &r.ColumnEnd, &r.ExecutionCount, &r.FileID, &r.ExpandedFileID, &r.Kind}
	wantLen := len(tmp)
	if err := json.Unmarshal(buf, &tmp); err != nil {
		return err
	}
	if l, k := len(tmp), wantLen; l != k {
		return fmt.Errorf("wrong number of fields in Region: %d != %d", l, k)
	}
	return nil
}

type Counts struct {
	Count      int     `json:"count"`
	Covered    int     `json:"covered"`
	NotCovered int     `json:"notcovered"`
	Percent    float32 `json:"percent"`
}
