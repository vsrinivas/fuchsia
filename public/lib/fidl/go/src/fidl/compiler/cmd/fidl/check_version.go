// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"crypto/sha1"
	"encoding/hex"
	"io/ioutil"
	"log"
	"path/filepath"
	"strings"
)

// checkVersion verifies the version of the mojom tool being executed
// corresponds to the sha1 checked in source control. It works as following:
//
// If there is no file mojom.sha1 in the same directory as the tool, the version
// check is bypassed.
// If there is such a file, that files is read and it is decoded assuming that
// it contains a hexadecimal-encoded sha1 hash. The sha1 hash of the mojom tool
// binary being run is then computed and compared against that in the file. If
// the comparison fails, an error message is logged and the program terminates
// with return code 1.
func checkVersion(args []string) {
	mojomToolPath := args[0]
	mojomDir := filepath.Dir(mojomToolPath)
	sha1FilePath := filepath.Join(mojomDir, "mojom.sha1")

	// First we read the file containing the sha1 hash.
	expectedSha1Bytes, err := ioutil.ReadFile(sha1FilePath)
	if err != nil {
		// Could not read mojom.sha1. Assume it does not exist.
		return
	}
	expectedSha1 := strings.TrimSpace(string(expectedSha1Bytes))

	// Read the binary mojom tool and compute its sha1.
	var mojomToolBytes []byte
	mojomToolBytes, err = ioutil.ReadFile(mojomToolPath)
	if err != nil {
		// Could not read the binary. Ignore version check.
		return
	}
	mojomToolSha1Bytes := sha1.Sum(mojomToolBytes)
	mojomToolSha1 := hex.EncodeToString(mojomToolSha1Bytes[:])

	if mojomToolSha1 != expectedSha1 {
		mojomToolAbsPath, err := filepath.Abs(mojomToolPath)
		if err == nil {
			mojomToolPath = mojomToolAbsPath
		}
		log.Fatalf("The version of the mojom tool at %s does not correspond to mojom.sha1 "+
			"in the same directory. Please update the mojom tool (run gclient sync).\n", mojomToolPath)
	}
}
