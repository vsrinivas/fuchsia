// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

import (
	"flag"
	"fmt"
	"testing"

	"go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/typestest"
	"go.fuchsia.dev/fuchsia/tools/fidl/fidlgen_go/ir"
)

var testDataFlag = flag.String("test_data_dir", "../../../../garnet/go/src/fidl/compiler/backend/goldens", "Path to golden; only used in GN build")

func TestCodegenImplDotGo(t *testing.T) {
	for _, filename := range typestest.AllExamples(*testDataFlag) {
		t.Run(filename, func(t *testing.T) {
			tree := ir.Compile(typestest.GetExample(*testDataFlag, filename))
			want := typestest.GetGolden(*testDataFlag, fmt.Sprintf("%s.go.golden", filename))
			got, err := NewGenerator().generateImplDotGo(tree)
			if err != nil {
				t.Fatalf("unexpected error while generating impl.go: %s", err)
			}
			typestest.AssertCodegenCmp(t, want, got)
		})
	}
}
