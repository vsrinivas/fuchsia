// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package symbolize

import (
	"flag"
	"fmt"
	"path/filepath"

	"go.fuchsia.dev/fuchsia/tools/debug/elflib"
)

var testDataDir = flag.String("test_data_dir", "testdata", "Path to testdata/; only used in GN build")

type mockSource []elflib.BinaryFileRef

func (m mockSource) GetBuildObject(buildID string) (FileCloser, error) {
	// Common binaries used for tests in this package.
	for _, file := range m {
		if file.BuildID == buildID {
			if err := file.Verify(); err != nil {
				return nil, err
			}
			return NopFileCloser(file.Filepath), nil
		}
	}
	return nil, fmt.Errorf("could not find file associated with %s", buildID)
}

func getTestBinaries() mockSource {
	return mockSource{
		{Filepath: filepath.Join(*testDataDir, "gobug.elf"), BuildID: "5bf6a28a259b95b4f20ffbcea0cbb149"},
		{Filepath: filepath.Join(*testDataDir, "libc.elf"), BuildID: "4fcb712aa6387724a9f465a32cd8c14b"},
		{Filepath: filepath.Join(*testDataDir, "libcrypto.elf"), BuildID: "12ef5c50b3ed3599c07c02d4509311be"},
	}
}
