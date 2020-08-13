// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package symbolize

import (
	"bufio"
	"fmt"
	"io"
	"path/filepath"
	"strings"
)

type Entry struct {
	SymbolPath string
	BuildDir   string
}
type Index []Entry

func LoadIndex(reader io.Reader) (Index, error) {
	var index Index
	scanner := bufio.NewScanner(reader)
	for scanner.Scan() {
		s := strings.SplitN(scanner.Text(), "\t", 2)
		if !filepath.IsAbs(s[0]) {
			return nil, fmt.Errorf("%q is not absolute", s[0])
		}
		entry := Entry{SymbolPath: s[0]}
		if len(s) > 1 {
			if !filepath.IsAbs(s[1]) {
				return nil, fmt.Errorf("%q is not absolute", s[1])
			}
			entry.BuildDir = s[1]
		}
		index = append(index, entry)
	}
	if err := scanner.Err(); err != nil {
		return nil, err
	}
	return index, nil
}
