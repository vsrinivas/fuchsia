// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bloaty

import (
	"encoding/csv"
	"fmt"
	"io"
	"strconv"
)

type row struct {
	Symbol string
	File   string
	Seg    string
	Vmsz   uint64
	Filesz uint64
}

// Symbol represents data about one symbol in the produced Bloaty output.
type Symbol struct {
	Name     string   `json:"Name"`
	Vmsz     uint64   `json:"Vmsz"`
	Filesz   uint64   `json:"Filesz"`
	Binaries []string `json:"Binaries"`
}

// File represents all data about one file in the produced Bloaty output
type File struct {
	Symbols     map[string]*Symbol `json:"Symbols"`
	TotalFilesz uint64             `json:"TotalFilesz"`
}

// Segment represents all data about one segment in the produced Bloaty output
type Segment struct {
	Files map[string]*File
}

func parseRow(rawRow []string) (row, bool, error) {
	var out row
	if len(rawRow) != 5 {
		return out, false, fmt.Errorf("row did not match format")
	}

	if rawRow[0] == "segments" {
		return out, true, nil
	}

	out.Seg = rawRow[0]
	out.File = rawRow[1]
	out.Symbol = rawRow[2]

	var err error
	out.Vmsz, err = strconv.ParseUint(rawRow[3], 0, 64)
	if err != nil {
		return out, false, err
	}

	out.Filesz, err = strconv.ParseUint(rawRow[4], 0, 64)
	if err != nil {
		return out, false, err
	}

	return out, false, nil
}

func filterRow(r row) bool {
	// These rows mean nothing to us
	if r.Vmsz == 0 || r.Filesz == 0 {
		return false
	}
	// These are things that get stripped away
	if r.Seg == "[Unmapped]" {
		return false
	}
	return true
}

func ReadCSV(data io.Reader, out chan<- bloatyOutput, file string) error {
	reader := csv.NewReader(data)
	reader.Comma = '\t'

	var line int
	for {
		r, err := reader.Read()
		if err == io.EOF {
			return nil
		}
		if err != nil {
			return fmt.Errorf("%v\n", err)
		}

		// If it's empty, ignore it
		if len(r) == 0 {
			line += 1
			continue
		}

		properRow, isHeader, err := parseRow(r)
		if !isHeader {
			if err != nil {
				return fmt.Errorf("unable to decode line %d:%v %v\n", line, r, err)
			}
			if filterRow(properRow) {
				out <- bloatyOutput{
					data: properRow,
					file: file,
				}
			}
		}
		line += 1
	}
}
