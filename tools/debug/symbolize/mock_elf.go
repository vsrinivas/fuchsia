// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package symbolize

import (
	"fmt"
	"log"
	"os"
	"path"
	"path/filepath"

	"go.fuchsia.dev/fuchsia/tools/debug/elflib"
)

type mockSource []elflib.BinaryFileRef

// Common binaries used for tests in this package.
var testBinaries = mockSource{
	{Filepath: getTestdataPath("gobug.elf"), BuildID: "5bf6a28a259b95b4f20ffbcea0cbb149"},
	{Filepath: getTestdataPath("libc.elf"), BuildID: "4fcb712aa6387724a9f465a32cd8c14b"},
	{Filepath: getTestdataPath("libcrypto.elf"), BuildID: "12ef5c50b3ed3599c07c02d4509311be"},
}

func getTestdataPath(filename string) string {
	testPath, err := filepath.Abs(os.Args[0])
	if err != nil {
		log.Fatalf("failed to get test path: %v", err)
	}
	outDir := filepath.Dir(testPath)
	return path.Join(outDir, "testdata", "symbolize", filename)
}

func (m mockSource) GetBuildObject(buildID string) (FileCloser, error) {
	for _, file := range testBinaries {
		if file.BuildID == buildID {
			if err := file.Verify(); err != nil {
				return nil, err
			}
			return NopFileCloser(file.Filepath), nil
		}
	}
	return nil, fmt.Errorf("could not find file associated with %s", buildID)
}
