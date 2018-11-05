// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package rust

import (
	"bytes"
	"fmt"
	"testing"

	"fidl/compiler/backend/rust/ir"
	"fidl/compiler/backend/typestest"
)

var cases = []string{
	"doc_comments.fidl.json",
	"tables.fidl.json",
}

func TestCodegen(t *testing.T) {
	for _, filename := range cases {
		t.Run(filename, func(t *testing.T) {
			fidl := typestest.GetExample(filename)
			tree := ir.Compile(fidl)
			implDotRs := typestest.GetGolden(fmt.Sprintf("%s.rs.golden", filename))

			actualImplDotRs := new(bytes.Buffer)
			if err := NewFidlGenerator().GenerateImpl(actualImplDotRs, tree); err != nil {
				t.Fatalf("unexpected error while generating impl.go: %s", err)
			}

			typestest.AssertCodegenCmp(t, implDotRs, actualImplDotRs.Bytes())
		})
	}
}
