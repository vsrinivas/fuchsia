// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

import (
	"bytes"
	"fmt"
	"os"
	"path/filepath"
	"testing"

	"fidl/compiler/backend/common"
	"fidl/compiler/backend/typestest"

	"go.fuchsia.dev/fuchsia/tools/fidl/fidlgen_rust/ir"
)

var testPath = func() string {
	testPath, err := filepath.Abs(os.Args[0])
	if err != nil {
		panic(err)
	}
	return filepath.Dir(testPath)
}()

var (
	basePath          = filepath.Join(testPath, "test_data", "fidlgen")
	rustfmtPath       = filepath.Join(testPath, "test_data", "fidlgen_rust", "rustfmt")
	rustfmtConfigPath = filepath.Join(testPath, "test_data", "fidlgen_rust", "rustfmt.toml")
)

type closeableBytesBuffer struct {
	bytes.Buffer
}

func (bb *closeableBytesBuffer) Close() error {
	return nil
}

func TestCodegen(t *testing.T) {
	for _, filename := range typestest.AllExamples(basePath) {
		t.Run(filename, func(t *testing.T) {
			fidl := typestest.GetExample(basePath, filename)
			tree := ir.Compile(fidl)
			implDotRs := typestest.GetGolden(basePath, fmt.Sprintf("%s.rs.golden", filename))

			actualImplDotRs := new(closeableBytesBuffer)
			formatter := common.NewFormatter(rustfmtPath, "--config-path", rustfmtConfigPath)
			actualFormattedImplDotRs, err := formatter.FormatPipe(actualImplDotRs)
			if err != nil {
				t.Fatalf("unable to create format pipe: %s", err)
			}
			if err := NewGenerator().GenerateImpl(actualFormattedImplDotRs, tree); err != nil {
				t.Fatalf("unexpected error while generating impl.go: %s", err)
			}
			if err := actualFormattedImplDotRs.Close(); err != nil {
				t.Fatalf("unexpected error while closing formatter: %s", err)
			}

			typestest.AssertCodegenCmp(t, implDotRs, actualImplDotRs.Bytes())
		})
	}
}
