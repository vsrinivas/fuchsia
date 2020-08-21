// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

import (
	"bytes"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/cpp"
	"go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/typestest"
)

// basePath holds the base path to the directory containing goldens.
var basePath = func() string {
	testPath, err := filepath.Abs(os.Args[0])
	if err != nil {
		panic(err)
	}
	return filepath.Join(filepath.Dir(testPath), "test_data", "fidlgen")
}()

type example string

func (s example) header() string {
	return fmt.Sprintf("%s.h.golden", s)
}

func (s example) source() string {
	return fmt.Sprintf("%s.cc.golden", s)
}

func (s example) testHeader() string {
	return fmt.Sprintf("%s_test_base.h.golden", s)
}

type closeableBytesBuffer struct {
	bytes.Buffer
}

func (bb *closeableBytesBuffer) Close() error {
	return nil
}

var testPath = func() string {
	testPath, err := filepath.Abs(os.Args[0])
	if err != nil {
		panic(err)
	}
	return filepath.Dir(testPath)
}()

var clangFormatPath = filepath.Join(testPath, "test_data", "fidlgen_hlcpp", "clang-format")

func TestCodegenHeader(t *testing.T) {
	for _, filename := range typestest.AllExamples(basePath) {
		t.Run(filename, func(t *testing.T) {
			fidl := typestest.GetExample(basePath, filename)
			tree := cpp.CompileHL(fidl)
			tree.PrimaryHeader = strings.TrimRight(example(filename).header(), ".golden")
			header := typestest.GetGolden(basePath, example(filename).header())

			buf := new(closeableBytesBuffer)
			formatterPipe, err := cpp.NewClangFormatter(clangFormatPath).FormatPipe(buf)
			if err != nil {
				t.Fatal(err)
			}
			if err := NewFidlGenerator().GenerateHeader(formatterPipe, tree); err != nil {
				t.Fatalf("unexpected error while generating header: %s", err)
			}
			formatterPipe.Close()

			typestest.AssertCodegenCmp(t, header, buf.Bytes())
		})
	}
}
func TestCodegenSource(t *testing.T) {
	for _, filename := range typestest.AllExamples(basePath) {
		t.Run(filename, func(t *testing.T) {
			fidl := typestest.GetExample(basePath, filename)
			tree := cpp.CompileHL(fidl)
			tree.PrimaryHeader = strings.TrimRight(example(filename).header(), ".golden")
			source := typestest.GetGolden(basePath, example(filename).source())

			buf := new(closeableBytesBuffer)
			formatterPipe, err := cpp.NewClangFormatter(clangFormatPath).FormatPipe(buf)
			if err != nil {
				t.Fatal(err)
			}
			if err := NewFidlGenerator().GenerateSource(formatterPipe, tree); err != nil {
				t.Fatalf("unexpected error while generating source: %s", err)
			}
			formatterPipe.Close()

			typestest.AssertCodegenCmp(t, source, buf.Bytes())
		})
	}
}

func TestCodegenTestHeader(t *testing.T) {
	for _, filename := range typestest.AllExamples(basePath) {
		t.Run(filename, func(t *testing.T) {
			fidl := typestest.GetExample(basePath, filename)
			tree := cpp.CompileHL(fidl)
			tree.PrimaryHeader = strings.TrimRight(example(filename).header(), ".golden")
			source := typestest.GetGolden(basePath, example(filename).testHeader())

			buf := new(closeableBytesBuffer)
			formatterPipe, err := cpp.NewClangFormatter(clangFormatPath).FormatPipe(buf)
			if err != nil {
				t.Fatal(err)
			}
			if err := NewFidlGenerator().GenerateTestBase(formatterPipe, tree); err != nil {
				t.Fatalf("unexpected error while generating source: %s", err)
			}
			formatterPipe.Close()

			typestest.AssertCodegenCmp(t, source, buf.Bytes())
		})
	}
}
