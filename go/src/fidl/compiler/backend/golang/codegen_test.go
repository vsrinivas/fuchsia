// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package golang

import (
	"testing"

	"fidl/compiler/backend/golang/ir"
	"fidl/compiler/backend/typestest"
)

var cases = map[string]string{
	"doc_comments.fidl.json": "doc_comments.fidl.json.go.golden",
}

func TestCodegenImplDotGo(t *testing.T) {
	for filename, expected := range cases {
		t.Run(filename, func(t *testing.T) {
			fidl := typestest.GetExample(filename)
			tree := ir.Compile(fidl)
			implDotGo := typestest.GetGolden(expected)

			actualImplDotGo, err := NewFidlGenerator().GenerateImplDotGo(tree)
			if err != nil {
				t.Fatalf("unexpected error while generating impl.go: %s", err)
			}

			typestest.AssertCodegenCmp(t, implDotGo, actualImplDotGo)
		})
	}
}
