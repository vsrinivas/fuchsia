// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

import (
	"bytes"
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"testing"

	"go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/cpp"
	"go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/typestest"
)

// hostPlatform reproduces the same format os-arch variant as cipd does.
func hostPlatform() string {
	o := runtime.GOOS
	if o == "darwin" {
		o = "mac"
	}
	switch a := runtime.GOARCH; a {
	case "amd64":
		return o + "-x64"
	default:
		return o + "-" + a
	}
}

var clangFormatFlag = flag.String("clang-format", "../../../../prebuilt/third_party/clang/"+hostPlatform()+"/bin", "Path to directory containing clang-format; only used in GN build")
var testDataFlag = flag.String("test_data_dir", "../../../../garnet/go/src/fidl/compiler/backend/goldens", "Path to golden; only used in GN build")

type example string

func (s example) primaryHeader() string {
	return fmt.Sprintf("%s.h", s)
}

func (s example) goldenHeader() string {
	return fmt.Sprintf("%s.libfuzzer.h.golden", s)
}

func (s example) goldenSource() string {
	return fmt.Sprintf("%s.libfuzzer.cc.golden", s)
}

func fileExists(filename string) bool {
	info, err := os.Stat(filename)
	return !os.IsNotExist(err) && !info.IsDir()
}

type closeableBytesBuffer struct {
	bytes.Buffer
}

func (bb *closeableBytesBuffer) Close() error {
	return nil
}

func TestCodegenHeader(t *testing.T) {
	clangFormat := filepath.Join(*clangFormatFlag, "clang-format")
	for _, filename := range typestest.AllExamples(*testDataFlag) {
		path := filepath.Join(*testDataFlag, example(filename).goldenHeader())
		if !fileExists(path) {
			t.Logf("No golden: %s", path)
			continue
		}
		t.Run(filename, func(t *testing.T) {
			fidl := typestest.GetExample(*testDataFlag, filename)
			tree := cpp.CompileLibFuzzer(fidl)
			prepareTree(fidl.Name, &tree)
			want := typestest.GetGolden(*testDataFlag, example(filename).goldenHeader())
			buf := closeableBytesBuffer{}
			formatterPipe, err := cpp.NewClangFormatter(clangFormat).FormatPipe(&buf)
			if err != nil {
				t.Fatal(err)
			}
			if err := NewFidlGenerator().GenerateHeader(formatterPipe, tree); err != nil {
				t.Fatalf("unexpected error while generating header: %s", err)
			}
			formatterPipe.Close()
			typestest.AssertCodegenCmp(t, want, buf.Bytes())
		})
	}
}

func TestCodegenSource(t *testing.T) {
	clangFormat := filepath.Join(*clangFormatFlag, "clang-format")
	for _, filename := range typestest.AllExamples(*testDataFlag) {
		path := filepath.Join(*testDataFlag, example(filename).goldenSource())
		if !fileExists(path) {
			t.Logf("No golden: %s", path)
			continue
		}
		t.Run(filename, func(t *testing.T) {
			fidl := typestest.GetExample(*testDataFlag, filename)
			tree := cpp.CompileLibFuzzer(fidl)
			prepareTree(fidl.Name, &tree)
			want := typestest.GetGolden(*testDataFlag, example(filename).goldenSource())
			buf := closeableBytesBuffer{}
			formatterPipe, err := cpp.NewClangFormatter(clangFormat).FormatPipe(&buf)
			if err != nil {
				t.Fatal(err)
			}
			if err := NewFidlGenerator().GenerateSource(formatterPipe, tree); err != nil {
				t.Fatalf("unexpected error while generating source: %s", err)
			}
			formatterPipe.Close()
			typestest.AssertCodegenCmp(t, want, buf.Bytes())
		})
	}
}
