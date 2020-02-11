// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package syzkaller

import (
	"fmt"
	"os"
	"path/filepath"
	"testing"

	"fidl/compiler/backend/syzkaller/ir"
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

func TestCodegenImplDotSyzkaller(t *testing.T) {
	for _, filename := range typestest.AllExamples(basePath) {
		// TODO(fxb/45007): Syzkaller does not support enum member references in
		// struct defaults.
		if filename == "struct_default_value_enum_library_reference.test.json" {
			continue
		}
		t.Run(filename, func(t *testing.T) {
			fidl := typestest.GetExample(basePath, filename)
			tree := ir.Compile(fidl)
			impl := typestest.GetGolden(basePath, fmt.Sprintf("%s.syz.txt.golden", filename))

			actualImpl, err := NewFidlGenerator().GenerateImpl(tree)
			if err != nil {
				t.Fatalf("unexpected error while generating impl.syz.txt: %s", err)
			}

			typestest.AssertCodegenCmp(t, impl, actualImpl)
		})
	}
}
