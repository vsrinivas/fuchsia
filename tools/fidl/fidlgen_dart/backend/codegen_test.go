// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package backend

import (
	"bytes"
	"fmt"
	"os"
	"path/filepath"
	"testing"

	"go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/common"
	"go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/typestest"
	"go.fuchsia.dev/fuchsia/tools/fidl/fidlgen_dart/backend/ir"
)

func testDataPath(paths ...string) string {
	testPath, err := filepath.Abs(os.Args[0])
	if err != nil {
		panic(err)
	}
	paths = append([]string{filepath.Dir(testPath), "test_data"}, paths...)
	return filepath.Join(paths...)
}

var (
	fildgenDartPath = testDataPath("fidlgen_dart")
	dartfmtPath     = testDataPath("fidlgen_dart", "dartfmt")
)

type closeableBytesBuffer struct {
	bytes.Buffer
}

func (bb *closeableBytesBuffer) Close() error {
	return nil
}

func TestCodegenAsyncLibrary(t *testing.T) {
	for _, filename := range typestest.AllExamples(fildgenDartPath) {
		t.Run(filename, func(t *testing.T) {
			expected := typestest.GetGolden(
				fildgenDartPath, fmt.Sprintf("%s_async.dart.golden", filename))

			tree := ir.Compile(typestest.GetExample(fildgenDartPath, filename))
			actualWr := new(closeableBytesBuffer)
			actualWrPipe, err := common.NewFormatter(dartfmtPath).FormatPipe(actualWr)
			if err != nil {
				t.Fatalf("unable to create format pipe: %s", err)
			}
			err = NewFidlGenerator().generateAsyncFile(actualWrPipe, tree)
			if err != nil {
				t.Fatalf("unexpected error while generating async library: %s", err)
			}
			if err := actualWrPipe.Close(); err != nil {
				t.Fatalf("unexpected error while closing formatter: %s", err)
			}

			typestest.AssertCodegenCmp(t, expected, actualWr.Bytes())
		})
	}
}

func TestCodegenTestFile(t *testing.T) {
	for _, filename := range typestest.AllExamples(fildgenDartPath) {
		t.Run(filename, func(t *testing.T) {
			expected := typestest.GetGolden(
				fildgenDartPath, fmt.Sprintf("%s_test.dart.golden", filename))

			tree := ir.Compile(typestest.GetExample(fildgenDartPath, filename))
			actualWr := new(closeableBytesBuffer)
			actualWrPipe, err := common.NewFormatter(dartfmtPath).FormatPipe(actualWr)
			if err != nil {
				t.Fatalf("unable to create format pipe: %s", err)
			}
			err = NewFidlGenerator().generateTestFile(actualWrPipe, tree)
			if err != nil {
				t.Fatalf("unexpected error while generating test file: %s", err)
			}
			if err := actualWrPipe.Close(); err != nil {
				t.Fatalf("unexpected error while closing formatter: %s", err)
			}

			typestest.AssertCodegenCmp(t, expected, actualWr.Bytes())
		})
	}
}
