// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

import (
	"bytes"
	"flag"
	"fmt"
	"path/filepath"
	"runtime"
	"strings"
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

func (s example) header() string {
	return fmt.Sprintf("%s.llcpp.h.golden", s)
}

func (s example) source() string {
	return fmt.Sprintf("%s.llcpp.cc.golden", s)
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
		t.Run(filename, func(t *testing.T) {
			tree := cpp.CompileLL(typestest.GetExample(*testDataFlag, filename))
			tree.PrimaryHeader = strings.TrimRight(example(filename).header(), ".golden")
			want := typestest.GetGolden(*testDataFlag, example(filename).header())
			buf := closeableBytesBuffer{}
			formatterPipe, err := cpp.NewClangFormatter(clangFormat).FormatPipe(&buf)
			if err != nil {
				t.Fatal(err)
			}
			if err := NewGenerator().generateHeader(formatterPipe, tree); err != nil {
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
		t.Run(filename, func(t *testing.T) {
			tree := cpp.CompileLL(typestest.GetExample(*testDataFlag, filename))
			tree.PrimaryHeader = strings.TrimRight(example(filename).header(), ".golden")
			want := typestest.GetGolden(*testDataFlag, example(filename).source())
			buf := closeableBytesBuffer{}
			formatterPipe, err := cpp.NewClangFormatter(clangFormat).FormatPipe(&buf)
			if err != nil {
				t.Fatal(err)
			}
			if err := NewGenerator().generateSource(formatterPipe, tree); err != nil {
				t.Fatalf("unexpected error while generating source: %s", err)
			}
			formatterPipe.Close()
			typestest.AssertCodegenCmp(t, want, buf.Bytes())
		})
	}
}
