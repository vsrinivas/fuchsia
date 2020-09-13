// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package elflib

import (
	"debug/dwarf"
	"debug/elf"
	"fmt"
	"io"
)

// ListSources reads the DWARF information in a given binary to determine the
// source locations that comprise it. The returned sources are deduped, but are
// in no particular order.
func ListSources(binary string) ([]string, error) {
	f, err := elf.Open(binary)
	if err != nil {
		return nil, fmt.Errorf("failed to open binary: %w", err)
	}
	defer f.Close()

	data, err := f.DWARF()
	if err != nil {
		return nil, fmt.Errorf("failed to obtain DWARF information: %w", err)
	}

	r := data.Reader()
	var srcs []string
	seen := make(map[string]bool)
	for {
		entry, err := r.Next()
		if err != nil {
			return nil, fmt.Errorf("failed to read entry: %w", err)
		} else if entry == nil {
			break // Last entry.
		}
		lr, err := data.LineReader(entry)
		if err != nil {
			return nil, fmt.Errorf("failed to create a line table reader: %w", err)
		} else if lr == nil {
			continue // No line table.
		}

		le := new(dwarf.LineEntry)
		for {
			err = lr.Next(le)
			if err == io.EOF {
				break // End of line table.
			} else if err != nil {
				return nil, fmt.Errorf("failed to read line table entry: %w", err)
			}
			for _, lf := range lr.Files() {
				if lf == nil { // First entry is always nil.
					continue
				} else if _, ok := seen[lf.Name]; ok {
					continue
				}
				srcs = append(srcs, lf.Name)
				seen[lf.Name] = true
			}
		}
	}
	return srcs, nil
}
