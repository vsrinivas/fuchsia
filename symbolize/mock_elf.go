// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package symbolize

import (
	"fuchsia.googlesource.com/tools/elflib"
)

type mockSource []elflib.BinaryFileRef

// Common binaries used for tests in this package.
var testBinaries = mockSource{
	{Filepath: "testdata/gobug.elf", BuildID: "5bf6a28a259b95b4f20ffbcea0cbb149"},
	{Filepath: "testdata/libc.elf", BuildID: "4fcb712aa6387724a9f465a32cd8c14b"},
	{Filepath: "testdata/libcrypto.elf", BuildID: "12ef5c50b3ed3599c07c02d4509311be"},
}

// GetBinaries implements Source.
func (m mockSource) getBinaries() ([]elflib.BinaryFileRef, error) {
	return []elflib.BinaryFileRef(m), nil
}
