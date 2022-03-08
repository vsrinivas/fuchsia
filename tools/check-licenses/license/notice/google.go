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

// NOTICE.txt files have the format:
//     [package name]
//     [license]
//     =================
//     [package name]
//     [license]
//     =================
// etc.

var separator = []byte("=================")

func ParseGoogle(noticeFile *file.File) ([]*Data, error) {
	// TODO: Consider having the file struct handle file opens and reads.
	r, err := os.Open(noticeFile.Path)
	if err != nil {
		return nil, err
	}
	defer r.Close()

	var (
		inLicense bool

		builder  strings.Builder
		licenses []*Data
	)

	license := &Data{}

	scanner := bufio.NewScanner(r)
	for scanner.Scan() {
		line := scanner.Bytes()
		if inLicense {
			if bytes.Equal(line, separator) {
				// End of license. Next line will be the package name.
				inLicense = false
				license.LicenseText = []byte(builder.String())
				licenses = append(licenses, license)
				license = &Data{}
				builder.Reset()
			} else {
				builder.Write(line)
				builder.WriteString("\n")
			}
		} else {
			// Current line is package name, license starts on next line.
			license.LibraryName = string(line)
			inLicense = true
		}
	}
	if err := scanner.Err(); err != nil {
		return nil, err
	}

	return licenses, nil
}
