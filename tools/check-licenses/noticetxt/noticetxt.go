// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package noticetxt

import (
	"bufio"
	"bytes"
	"io"
	"os"
)

// NOTICE.txt files have the format:
//     [package name]
//     [license]
//     =================
//     [package name]
//     [license]
//     =================
// etc.

var separator = []byte("=================")

// ParseNoticeTxtFile returns the licenses found in NOTICE.txt file at `path` as byte slices.
func ParseNoticeTxtFile(path string) ([][]byte, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	return parseNoticeTxtFile(f)
}

func parseNoticeTxtFile(r io.Reader) ([][]byte, error) {
	var (
		license   []byte
		licenses  [][]byte
		inLicense bool
	)

	scanner := bufio.NewScanner(r)
	for scanner.Scan() {
		line := scanner.Bytes()
		if inLicense {
			if bytes.Equal(line, separator) {
				// End of license. Next line will be the package name.
				inLicense = false
				licenses = append(licenses, license)
				license = []byte{}
			} else {
				license = append(license, line...)
				license = append(license, '\n')
			}
		} else {
			// Current line is package name, license starts on next line.
			inLicense = true
		}
	}
	if err := scanner.Err(); err != nil {
		return nil, err
	}

	return licenses, nil
}
