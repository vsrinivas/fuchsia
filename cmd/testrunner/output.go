// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"archive/tar"
	"bytes"
	"fmt"
	"io"
	"os"
	"path"
	"strings"

	"fuchsia.googlesource.com/tools/botanist"
	"fuchsia.googlesource.com/tools/runtests"
	"fuchsia.googlesource.com/tools/tap"
	"fuchsia.googlesource.com/tools/testrunner"
)

// TarOutput records test results in a TAR archive.
type TarOutput struct {
	w *tar.Writer
}

func (o *TarOutput) Record(result testrunner.TestResult) {
	pathInArchive := path.Join(result.Name, runtests.TestOutputFilename)
	stdout := bytes.NewReader(result.Stdout)
	stderr := bytes.NewReader(result.Stderr)
	botanist.ArchiveReader(o.w, io.MultiReader(stdout, stderr), pathInArchive)
}

// TarFile adds a file to the underlying archive.
func (o *TarOutput) TarFile(bytes []byte, filename string) error {
	return botanist.ArchiveBuffer(o.w, bytes, filename)
}

func (o *TarOutput) Close() error {
	return o.w.Close()
}

func NewTarOutput(archive string) (*TarOutput, error) {
	fd, err := os.Create(archive)
	if err != nil {
		return nil, fmt.Errorf("failed to open %q: %v", archive, err)
	}

	tw := tar.NewWriter(fd)
	return &TarOutput{w: tw}, nil
}

// TAPOutput records test results as a TAP output stream.
type TAPOutput struct {
	producer *tap.Producer
}

func NewTAPOutput(output io.Writer, testCount int) *TAPOutput {
	producer := tap.NewProducer(output)
	producer.Plan(testCount)
	return &TAPOutput{producer}
}

func (o *TAPOutput) Record(result testrunner.TestResult) {
	o.producer.Ok(result.Result == runtests.TestSuccess, result.Name)
}

// SummaryOutput records test results in a TestSummary object.
type SummaryOutput struct {
	Summary runtests.TestSummary
}

func (o *SummaryOutput) Record(result testrunner.TestResult) {
	pathInArchive := path.Join(result.Name, runtests.TestOutputFilename)
	// Strip any leading //, contributed by Linux/Mac test names, so that
	// pathInArchive gives a valid relative path.
	pathInArchive = strings.TrimLeft(pathInArchive, "//")
	o.Summary.Tests = append(o.Summary.Tests, runtests.TestDetails{
		Name:       result.Name,
		OutputFile: pathInArchive,
		Result:     result.Result,
	})
}

// AddFile registers a file on disk as a file to include in the summary.
func (o *SummaryOutput) AddFile(name, path string) {
		if o.Summary.Outputs == nil {
			o.Summary.Outputs = make(map[string]string)
		}
		o.Summary.Outputs[name] = path
}
