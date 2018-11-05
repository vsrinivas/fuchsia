// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package cpp

import (
	"bytes"
	"fmt"
	"strings"
	"testing"

	"fidl/compiler/backend/cpp/ir"
	"fidl/compiler/backend/typestest"
)

type example string

func (s example) header() string {
	return fmt.Sprintf("%s.h.golden", s)
}

func (s example) source() string {
	return fmt.Sprintf("%s.cc.golden", s)
}

func (s example) testHeader() string {
	return fmt.Sprintf("%s_test_base.h.golden", s)
}

var cases = []example{
	"doc_comments.fidl.json",
	"tables.fidl.json",
}

func TestCodegenHeader(t *testing.T) {
	for _, filename := range cases {
		t.Run(string(filename), func(t *testing.T) {
			fidl := typestest.GetExample(string(filename))
			tree := ir.Compile(fidl)
			tree.PrimaryHeader = strings.TrimRight(filename.header(), ".golden")
			header := typestest.GetGolden(filename.header())

			buf := new(bytes.Buffer)
			if err := NewFidlGenerator().GenerateHeader(buf, tree); err != nil {
				t.Fatalf("unexpected error while generating header: %s", err)
			}

			typestest.AssertCodegenCmp(t, header, buf.Bytes())
		})
	}
}
func TestCodegenSource(t *testing.T) {
	for _, filename := range cases {
		t.Run(string(filename), func(t *testing.T) {
			fidl := typestest.GetExample(string(filename))
			tree := ir.Compile(fidl)
			tree.PrimaryHeader = strings.TrimRight(filename.header(), ".golden")
			source := typestest.GetGolden(filename.source())

			buf := new(bytes.Buffer)
			if err := NewFidlGenerator().GenerateSource(buf, tree); err != nil {
				t.Fatalf("unexpected error while generating source: %s", err)
			}

			typestest.AssertCodegenCmp(t, source, buf.Bytes())
		})
	}
}

func TestCodegenTestHeader(t *testing.T) {
	for _, filename := range cases {
		t.Run(string(filename), func(t *testing.T) {
			fidl := typestest.GetExample(string(filename))
			tree := ir.Compile(fidl)
			tree.PrimaryHeader = strings.TrimRight(filename.header(), ".golden")
			source := typestest.GetGolden(filename.testHeader())

			buf := new(bytes.Buffer)
			if err := NewFidlGenerator().GenerateTestBase(buf, tree); err != nil {
				t.Fatalf("unexpected error while generating source: %s", err)
			}

			typestest.AssertCodegenCmp(t, source, buf.Bytes())
		})
	}
}
