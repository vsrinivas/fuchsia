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

	"fidl/compiler/backend/cpp"
	"fidl/compiler/backend/typestest"
)

// basePath holds the base path to the directory containing goldens.
var basePath = func() string {
	testPath, err := filepath.Abs(os.Args[0])
	if err != nil {
		panic(err)
	}
	testDataDir := filepath.Join(filepath.Dir(testPath), "test_data", "fidlgen")
	return fmt.Sprintf("%s%c", testDataDir, filepath.Separator)
}()

type example string

func (s example) header() string {
	return fmt.Sprintf("%s.llcpp.h.golden", s)
}

func (s example) source() string {
	return fmt.Sprintf("%s.llcpp.cc.golden", s)
}

func TestCodegenHeader(t *testing.T) {
	for _, filename := range typestest.AllExamples(basePath) {
		t.Run(filename, func(t *testing.T) {
			fidl := typestest.GetExample(basePath, filename)
			tree := cpp.CompileLL(fidl)
			tree.PrimaryHeader = strings.TrimRight(example(filename).header(), ".golden")
			header := typestest.GetGolden(basePath, example(filename).header())

			buf := new(bytes.Buffer)
			if err := NewGenerator().generateHeader(buf, tree); err != nil {
				t.Fatalf("unexpected error while generating header: %s", err)
			}

			typestest.AssertCodegenCmp(t, header, buf.Bytes())
		})
	}
}
func TestCodegenSource(t *testing.T) {
	for _, filename := range typestest.AllExamples(basePath) {
		t.Run(filename, func(t *testing.T) {
			fidl := typestest.GetExample(basePath, filename)
			tree := cpp.CompileLL(fidl)
			tree.PrimaryHeader = strings.TrimRight(example(filename).header(), ".golden")
			source := typestest.GetGolden(basePath, example(filename).source())

			buf := new(bytes.Buffer)
			if err := NewGenerator().generateSource(buf, tree); err != nil {
				t.Fatalf("unexpected error while generating source: %s", err)
			}

			typestest.AssertCodegenCmp(t, source, buf.Bytes())
		})
	}
}
