// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package notice

import (
	"bufio"
	"bytes"
	"os"
	"strings"
)

// Chromium license files have the format:
//
// <chromium BSD-3 license>
// --------------------
// library name
// --------------------
// license text
//
// --------------------
// library name
// --------------------
// license text
//
// etc

const (
	parserStateLibraryName = iota
	parserStateLicense     = iota
)

func ParseChromium(path string) ([]*Data, error) {
	var licenseDelimiter = []byte("--------------------")

	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()

	var builder strings.Builder
	var licenses []*Data
	license := &Data{
		LibraryName: "Chromium",
	}

	// The first license in the file is the Chromium license, which should get the same treatment as
	// all of the third party licenses. The only difference is that it doesn't have a library name
	// associated with it.
	parserState := parserStateLicense

	scanner := bufio.NewScanner(f)
	lineNumber := 0
	for scanner.Scan() {
		lineNumber = lineNumber + 1
		line := scanner.Bytes()
		switch parserState {
		case parserStateLibraryName:
			if bytes.Equal(line, licenseDelimiter) {
				parserState = parserStateLicense
			} else {
				license.LibraryName = string(line)
				license.LineNumber = lineNumber
			}

		case parserStateLicense:
			if bytes.Equal(line, licenseDelimiter) {
				license.LicenseText = []byte(builder.String())
				licenses = append(licenses, license)
				license = &Data{}
				builder.Reset()
				parserState = parserStateLibraryName
			} else {
				builder.Write(line)
				builder.WriteString("\n")
			}
		}
	}

	return licenses, nil
}
