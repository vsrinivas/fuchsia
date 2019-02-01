// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package golang

import (
	"fmt"
	"testing"

	"fidl/compiler/backend/golang/ir"
	"fidl/compiler/backend/typestest"
)

func TestCodegenImplDotGo(t *testing.T) {
	for _, filename := range typestest.AllExamples() {
		t.Run(filename, func(t *testing.T) {
			fidl := typestest.GetExample(filename)
			tree := ir.Compile(fidl)
			implDotGo := typestest.GetGolden(fmt.Sprintf("%s.go.golden", filename))

			actualImplDotGo, err := NewFidlGenerator().GenerateImplDotGo(tree)
			if err != nil {
				t.Fatalf("unexpected error while generating impl.go: %s", err)
			}

			typestest.AssertCodegenCmp(t, implDotGo, actualImplDotGo)
		})
	}
}
