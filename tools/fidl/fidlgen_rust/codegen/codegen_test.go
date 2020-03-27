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

	"fidl/compiler/backend/typestest"

	"fidlgen_rust/ir"
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

func TestCodegen(t *testing.T) {
	for _, filename := range typestest.AllExamples(basePath) {
		t.Run(filename, func(t *testing.T) {
			fidl := typestest.GetExample(basePath, filename)
			tree := ir.Compile(fidl)
			implDotRs := typestest.GetGolden(basePath, fmt.Sprintf("%s.rs.golden", filename))

			actualImplDotRs := new(bytes.Buffer)
			if err := NewGenerator().GenerateImpl(actualImplDotRs, tree); err != nil {
				t.Fatalf("unexpected error while generating impl.go: %s", err)
			}

			typestest.AssertCodegenCmp(t, implDotRs, actualImplDotRs.Bytes())
		})
	}
}
