// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package cpp_overnet_embedded

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
	return fmt.Sprintf("%s.overnet_embedded.h.golden", s)
}

func (s example) source() string {
	return fmt.Sprintf("%s.overnet_embedded.cpp.golden", s)
}

func TestCodegenHeader(t *testing.T) {
	for _, filename := range typestest.AllExamples() {
		t.Run(filename, func(t *testing.T) {
			fidl := typestest.GetExample(filename)
			tree := ir.Compile(fidl)
			tree.PrimaryHeader = strings.TrimRight(example(filename).header(), ".golden")
			header := typestest.GetGolden(example(filename).header())

			buf := new(bytes.Buffer)
			if err := NewFidlGenerator().GenerateHeader(buf, tree); err != nil {
				t.Fatalf("unexpected error while generating header: %s", err)
			}

			typestest.AssertCodegenCmp(t, header, buf.Bytes())
		})
	}
}
func TestCodegenSource(t *testing.T) {
	for _, filename := range typestest.AllExamples() {
		t.Run(filename, func(t *testing.T) {
			fidl := typestest.GetExample(filename)
			tree := ir.Compile(fidl)
			tree.PrimaryHeader = strings.TrimRight(example(filename).header(), ".golden")
			source := typestest.GetGolden(example(filename).source())

			buf := new(bytes.Buffer)
			if err := NewFidlGenerator().GenerateSource(buf, tree); err != nil {
				t.Fatalf("unexpected error while generating source: %s", err)
			}

			typestest.AssertCodegenCmp(t, source, buf.Bytes())
		})
	}
}
