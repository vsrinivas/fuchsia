// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen_test

import (
	"bytes"
	"flag"
	"fmt"
	"testing"

	"go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/typestest"
	"go.fuchsia.dev/fuchsia/tools/fidl/fidlgen_syzkaller/codegen"
)

var testDataFlag = flag.String("test_data_dir", "../../../../garnet/go/src/fidl/compiler/backend/goldens", "Path to golden; only used in GN build")

func TestCodegenImplDotSyzkaller(t *testing.T) {
	for _, filename := range typestest.AllExamples(*testDataFlag) {
		t.Run(filename, func(t *testing.T) {
			if filename == "struct_default_value_enum_library_reference.test.json" {
				t.Skip("TODO(fxbug.dev/45007): Syzkaller does not support enum member references in struct defaults")
			}
			root := typestest.GetExample(*testDataFlag, filename)
			golden := typestest.GetGolden(*testDataFlag, fmt.Sprintf("%s.syz.txt.golden", filename))

			var buf bytes.Buffer
			if err := codegen.Compile(&buf, root); err != nil {
				t.Fatalf("error compiling example: %v", err)
			}
			typestest.AssertCodegenCmp(t, golden, buf.Bytes())
		})
	}
}
