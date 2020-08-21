// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

import (
	"fmt"
	"os"
	"path/filepath"
	"testing"

	"go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/typestest"
	"go.fuchsia.dev/fuchsia/tools/fidl/fidlgen_go/ir"
)

// basePath holds the base path to the directory containing goldens.
var basePath = func() string {
	testPath, err := filepath.Abs(os.Args[0])
	if err != nil {
		panic(err)
	}
	return filepath.Join(filepath.Dir(testPath), "test_data", "fidlgen")
}()

func TestCodegenImplDotGo(t *testing.T) {
	for _, filename := range typestest.AllExamples(basePath) {
		t.Run(filename, func(t *testing.T) {
			fidl := typestest.GetExample(basePath, filename)
			tree := ir.Compile(fidl)
			implDotGo := typestest.GetGolden(basePath, fmt.Sprintf("%s.go.golden", filename))

			actualImplDotGo, err := NewGenerator().generateImplDotGo(tree)
			if err != nil {
				t.Fatalf("unexpected error while generating impl.go: %s", err)
			}

			typestest.AssertCodegenCmp(t, implDotGo, actualImplDotGo)
		})
	}
}
