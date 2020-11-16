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

func (s example) header(suffix string) string {
	return fmt.Sprintf("%s%s.h.golden", s, suffix)
}

func (s example) source(suffix string) string {
	return fmt.Sprintf("%s%s.cc.golden", s, suffix)
}

func (s example) testHeader(suffix string) string {
	return fmt.Sprintf("%s%s_test_base.h.golden", s, suffix)
}

type closeableBytesBuffer struct {
	bytes.Buffer
}

func (bb *closeableBytesBuffer) Close() error {
	return nil
}

type variant struct {
	description    string
	filenameSuffix string
	CodeGenerationMode
	domainObjectIncludeStem string
}

var variants = []variant{
	{
		description:        "monolithic",
		CodeGenerationMode: Monolithic,
	},
	// Note that the filename suffixes need to stay in sync with
	// //garnet/go/src/fidl/compiler/backend/typestest/regen.sh
	{
		description:        "only-domain-objects",
		filenameSuffix:     ".natural_types",
		CodeGenerationMode: OnlyGenerateDomainObjects,
	},
}

func TestCodegenHeader(t *testing.T) {
	clangFormat := filepath.Join(*clangFormatFlag, "clang-format")
	for _, filename := range typestest.AllExamples(*testDataFlag) {
		for _, variant := range variants {
			t.Run(filename+"/"+variant.description, func(t *testing.T) {
				tree := cpp.CompileHL(typestest.GetExample(*testDataFlag, filename))
				tree.PrimaryHeader = strings.TrimRight(example(filename).header(variant.filenameSuffix), ".golden")
				tree.IncludeStem = "cpp/fidl"
				want := typestest.GetGolden(*testDataFlag, example(filename).header(variant.filenameSuffix))
				buf := closeableBytesBuffer{}
				formatterPipe, err := cpp.NewClangFormatter(clangFormat).FormatPipe(&buf)
				if err != nil {
					t.Fatal(err)
				}
				if err := NewFidlGenerator(variant.CodeGenerationMode).GenerateHeader(formatterPipe, tree); err != nil {
					t.Fatalf("unexpected error while generating header: %s", err)
				}
				formatterPipe.Close()
				typestest.AssertCodegenCmp(t, want, buf.Bytes())
			})
		}
	}
}

func TestCodegenSource(t *testing.T) {
	clangFormat := filepath.Join(*clangFormatFlag, "clang-format")
	for _, filename := range typestest.AllExamples(*testDataFlag) {
		for _, variant := range variants {
			t.Run(filename+"/"+variant.description, func(t *testing.T) {
				tree := cpp.CompileHL(typestest.GetExample(*testDataFlag, filename))
				tree.PrimaryHeader = strings.TrimRight(example(filename).header(variant.filenameSuffix), ".golden")
				tree.IncludeStem = "cpp/fidl"
				want := typestest.GetGolden(*testDataFlag, example(filename).source(variant.filenameSuffix))
				buf := closeableBytesBuffer{}
				formatterPipe, err := cpp.NewClangFormatter(clangFormat).FormatPipe(&buf)
				if err != nil {
					t.Fatal(err)
				}
				if err := NewFidlGenerator(variant.CodeGenerationMode).GenerateSource(formatterPipe, tree); err != nil {
					t.Fatalf("unexpected error while generating source: %s", err)
				}
				formatterPipe.Close()
				typestest.AssertCodegenCmp(t, want, buf.Bytes())
			})
		}
	}
}

func TestCodegenTestHeader(t *testing.T) {
	clangFormat := filepath.Join(*clangFormatFlag, "clang-format")
	for _, filename := range typestest.AllExamples(*testDataFlag) {
		for _, variant := range variants {
			if variant.CodeGenerationMode == OnlyGenerateDomainObjects {
				// The test header is not generated for domain objects.
				continue
			}
			t.Run(filename+"/"+variant.description, func(t *testing.T) {
				tree := cpp.CompileHL(typestest.GetExample(*testDataFlag, filename))
				tree.PrimaryHeader = strings.TrimRight(example(filename).header(variant.filenameSuffix), ".golden")
				tree.IncludeStem = "cpp/fidl"
				want := typestest.GetGolden(*testDataFlag, example(filename).testHeader(variant.filenameSuffix))
				buf := closeableBytesBuffer{}
				formatterPipe, err := cpp.NewClangFormatter(clangFormat).FormatPipe(&buf)
				if err != nil {
					t.Fatal(err)
				}
				if err := NewFidlGenerator(variant.CodeGenerationMode).GenerateTestBase(formatterPipe, tree); err != nil {
					t.Fatalf("unexpected error while generating source: %s", err)
				}
				formatterPipe.Close()
				typestest.AssertCodegenCmp(t, want, buf.Bytes())
			})
		}
	}
}
