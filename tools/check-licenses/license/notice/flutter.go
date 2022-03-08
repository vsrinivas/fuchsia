// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package notice

import (
	"bufio"
	"bytes"
	"os"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/check-licenses/file"
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

func ParseFlutter(noticeFile *file.File) ([]*Data, error) {
	// TODO: Consider having the file struct handle file opens and reads.
	f, err := os.Open(noticeFile.Path)
	if err != nil {
		return nil, err
	}
	defer f.Close()

	scanner := bufio.NewScanner(f)
	var (
		inAppendix bool
		inLicense  bool
		inSection  bool

		builder  strings.Builder
		licenses []*Data
	)

	license := &Data{}

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
					license.LicenseText = []byte(builder.String())
					licenses = append(licenses, license)
					license = &Data{}
					builder.Reset()
				} else if !inAppendix {
					builder.Write(line)
					builder.WriteString("\n")
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
