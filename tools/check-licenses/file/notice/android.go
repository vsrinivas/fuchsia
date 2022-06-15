// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package notice

import (
	"bufio"
	"bytes"
	"strings"
)

// ===========================================================
// Notices for file(s):
// <filepath>
// <filepath>
// <filepath>
//
// The notices is included for the library: <library name>
//
// ===========================================================
// <license text>
// ===========================================================
// etc

var (
	aemuDelimiter = []byte("===========================================================")
)

func ParseAndroid(path string, content []byte) ([]*Data, error) {
	r := bytes.NewReader(content)

	scanner := bufio.NewScanner(r)
	var (
		inHeader  bool
		inLicense bool

		builder  strings.Builder
		licenses []*Data
	)

	license := &Data{}
	inHeader = false
	inLicense = true

	lineNumber := 0
	for scanner.Scan() {
		lineNumber = lineNumber + 1
		line := scanner.Bytes()
		if inLicense {
			if bytes.Equal(line, aemuDelimiter) {
				inHeader = true
				inLicense = false
				license.LicenseText = []byte(builder.String())
				license.LineNumber = lineNumber + 1
				licenses = append(licenses, license)
				license = &Data{}
				builder.Reset()
			} else {
				builder.Write(line)
				builder.WriteString("\n")
			}
		} else if inHeader {
			if bytes.Equal(line, aemuDelimiter) {
				inHeader = false
				inLicense = true
			} else if bytes.Contains(line, []byte("The notices is included for the library:")) {
				name := bytes.TrimSpace(bytes.Split(line, []byte(":"))[1])
				license.LibraryName = string(name)
			}
		}
	}
	if err := scanner.Err(); err != nil {
		return nil, err
	}

	if inLicense {
		license.LicenseText = []byte(builder.String())
		licenses = append(licenses, license)
		license = &Data{}
		builder.Reset()
	}

	return licenses, nil
}
