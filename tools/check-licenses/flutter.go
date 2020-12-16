// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package checklicenses

import (
	"bufio"
	"bytes"
	"os"
)

// Flutter license files have the format:
//     ...
//     sectionDelimiter
//     ...
//     licenseDelimiter
//     license
//     appendixDelimiter?
//     appendix?
//     sectionDelimiter
//
//     sectionDelimiter
//     ...
//     licenseDelimiter
//     license
//     appendixDelimiter?
//     appendix?
//     sectionDelimiter
// etc.

var (
	sectionDelimiter  = []byte("====================================================================================================")
	licenseDelimiter  = []byte("----------------------------------------------------------------------------------------------------")
	appendixDelimiter = []byte("END OF TERMS AND CONDITIONS")
)

func processFlutterLicenses(licenses *Licenses, config *Config, metrics *Metrics, file_tree *FileTree) error {
	for _, path := range config.FlutterLicenses {
		data, err := parseFlutterLicenseFile(path)
		if err != nil {
			return nil
		}
		for _, d := range data {
			licenses.MatchSingleLicenseFile(d, path, metrics, file_tree)
		}
	}
	return nil
}

// parseFlutterLicenseFile returns the licenses found in the file as byte slices.
func parseFlutterLicenseFile(path string) ([][]byte, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()

	scanner := bufio.NewScanner(f)
	var (
		licenses   [][]byte
		inAppendix bool
		inLicense  bool
		inSection  bool
		license    []byte
	)

	for scanner.Scan() {
		line := scanner.Bytes()
		if inSection {
			if inLicense {
				if bytes.Equal(line, appendixDelimiter) {
					inAppendix = true
				} else if bytes.Equal(line, sectionDelimiter) {
					inAppendix = false
					inLicense = false
					inSection = false
					licenses = append(licenses, license)
					license = []byte{}
				} else if !inAppendix {
					license = append(license, line...)
					license = append(license, byte('\n'))
				}
			} else if bytes.Equal(line, licenseDelimiter) {
				inLicense = true
			}
		} else if bytes.Equal(line, sectionDelimiter) {
			inSection = true
		}
	}
	if err := scanner.Err(); err != nil {
		return nil, err
	}

	return licenses, nil
}
