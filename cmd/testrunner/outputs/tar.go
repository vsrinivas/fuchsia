// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package outputs

import (
	"archive/tar"
	"bytes"
	"io"
	"path"

	"fuchsia.googlesource.com/tools/tarutil"
	"fuchsia.googlesource.com/tools/runtests"
	"fuchsia.googlesource.com/tools/testrunner"
)

// TarOutput records test stdout and stderr streams in a TAR archive.
type TarOutput struct {
	w *tar.Writer
}

func NewTarOutput(w io.Writer) *TarOutput {
	tw := tar.NewWriter(w)
	return &TarOutput{w: tw}
}

// Record writes the given test result's stdout and stderr streams to the same file within
// a Tar archive.
func (o *TarOutput) Record(result testrunner.TestResult) {
	pathInArchive := path.Join(result.Name, runtests.TestOutputFilename)
	stdout := bytes.NewReader(result.Stdout)
	stderr := bytes.NewReader(result.Stderr)
	tarutil.TarReader(o.w, io.MultiReader(stdout, stderr), pathInArchive)
}

// TarFile adds a file to the underlying archive.
func (o *TarOutput) TarFile(bytes []byte, filename string) error {
	return tarutil.TarBuffer(o.w, bytes, filename)
}

// Close flushes all data to the archive.
func (o *TarOutput) Close() error {
	return o.w.Close()
}
