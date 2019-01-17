// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"archive/tar"
	"fmt"
	"io"
	"os"
	"path"

	"fuchsia.googlesource.com/tools/botanist"
	"fuchsia.googlesource.com/tools/runtests"
	"fuchsia.googlesource.com/tools/tap"
)

// TarRecorder records test results in a TAR archive.
type TarRecorder struct {
	Writer *tar.Writer
}

func (r *TarRecorder) Record(result testResult) {
	pathInArchive := path.Join(result.Name, runtests.TestOutputFilename)
	botanist.ArchiveReader(r.Writer, result.Output, pathInArchive)
}

func NewTarRecorder(archive string) (*TarRecorder, error) {
	fd, err := os.Create(archive)
	if err != nil {
		return nil, fmt.Errorf("failed to open %q: %v", archive, err)
	}

	tw := tar.NewWriter(fd)
	return &TarRecorder{Writer: tw}, nil
}

// TAPRecorder records test results as a TAP output stream.
type TAPRecorder struct {
	producer *tap.Producer
}

func NewTAPRecorder(output io.Writer, testCount int) *TAPRecorder {
	producer := tap.NewProducer(os.Stdout)
	producer.Plan(testCount)
	return &TAPRecorder{producer}
}

func (r *TAPRecorder) Record(result testResult) {
	r.producer.Ok(result.Result == runtests.TestSuccess, result.Name)
}

// SummaryRecorder records test results in a TestSummary object.
type SummaryRecorder struct {
	Summary runtests.TestSummary
}

func (r *SummaryRecorder) Record(result testResult) {
	pathInArchive := path.Join(result.Name, runtests.TestOutputFilename)
	r.Summary.Tests = append(r.Summary.Tests, runtests.TestDetails{
		Name:       result.Name,
		OutputFile: pathInArchive,
		Result:     result.Result,
	})
}
