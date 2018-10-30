// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package cpp

import (
	"bytes"
	"strings"
	"testing"

	"fidl/compiler/backend/cpp/ir"
	"fidl/compiler/backend/typestest"
)

var cases = map[string]struct {
	header, source string
}{
	"doc_comments.fidl.json": {
		header: "doc_comments.fidl.json.h.golden",
		source: "doc_comments.fidl.json.cc.golden",
	},
}

func TestCodegenHeader(t *testing.T) {
	for filename, expected := range cases {
		t.Run(filename, func(t *testing.T) {
			fidl := typestest.GetExample(filename)
			tree := ir.Compile(fidl)
			tree.PrimaryHeader = strings.TrimRight(expected.header, ".golden")
			header := typestest.GetGolden(expected.header)

			buf := new(bytes.Buffer)
			if err := NewFidlGenerator().GenerateHeader(buf, tree); err != nil {
				t.Fatalf("unexpected error while generating header: %s", err)
			}

			typestest.AssertCodegenCmp(t, header, buf.Bytes())
		})
	}
}
func TestCodegenSource(t *testing.T) {
	for filename, expected := range cases {
		t.Run(filename, func(t *testing.T) {
			fidl := typestest.GetExample(filename)
			tree := ir.Compile(fidl)
			tree.PrimaryHeader = strings.TrimRight(expected.header, ".golden")
			source := typestest.GetGolden(expected.source)

			buf := new(bytes.Buffer)
			if err := NewFidlGenerator().GenerateSource(buf, tree); err != nil {
				t.Fatalf("unexpected error while generating source: %s", err)
			}

			typestest.AssertCodegenCmp(t, source, buf.Bytes())
		})
	}
}
