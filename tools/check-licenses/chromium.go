// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package checklicenses

import (
	"bufio"
	"bytes"
	"os"
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

func processChromiumLicenses(licenses *Licenses, config *Config, metrics *Metrics, file_tree *FileTree) error {
	for _, path := range config.ChromiumLicenses {
		data, err := parseChromiumLicenseFile(path)
		if err != nil {
			return err
		}
		for _, d := range data {
			licenses.MatchSingleLicenseFile(d, path, metrics, file_tree)
		}
	}

	return nil
}

const (
	ParserStateLibraryName = iota
	ParserStateLicense     = iota
)

func parseChromiumLicenseFile(path string) ([][]byte, error) {
	var licenseDelimiter = []byte("--------------------")

	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()

	// The first license in the file is the Chromium license, which should get the same treatment as
	// all of the third party licenses. The only difference is that it doesn't have a library name
	// associated with it, but we ignore those anyway.
	parserState := ParserStateLicense
	licenses := [][]byte{}
	license := []byte{}

	scanner := bufio.NewScanner(f)
	for scanner.Scan() {
		line := scanner.Bytes()
		switch parserState {
		case ParserStateLibraryName:
			if bytes.Equal(line, licenseDelimiter) {
				parserState = ParserStateLicense
			}

		case ParserStateLicense:
			if bytes.Equal(line, licenseDelimiter) {
				licenses = append(licenses, license)
				license = []byte{}
				parserState = ParserStateLibraryName
			} else {
				license = append(license, line...)
				license = append(license, byte('\n'))
			}
		}
	}

	return licenses, nil
}
