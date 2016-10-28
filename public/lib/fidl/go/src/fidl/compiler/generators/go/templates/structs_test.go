// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

import (
	"testing"

	"mojom/generators/go/translator"
)

func TestStructDecl(t *testing.T) {
	expected := `type Foo struct {
	Alpha string
	Beta  uint32
}`

	s := translator.StructTemplate{
		Name: "Foo",
		Fields: []translator.StructFieldTemplate{
			{Name: "Alpha", Type: "string"},
			{Name: "Beta", Type: "uint32"},
		},
	}

	check(t, expected, "StructDecl", s)
}

func TestDecodingStructVersions(t *testing.T) {
	expected := `var someStruct_Versions []bindings.DataHeader = []bindings.DataHeader{
	bindings.DataHeader{80, 0},
	bindings.DataHeader{100, 1},
	bindings.DataHeader{120, 2},
}`

	type structVersion struct {
		NumBytes uint32
		Version  uint32
	}

	s := struct {
		PrivateName string
		Versions    []structVersion
	}{
		PrivateName: "someStruct",
		Versions: []structVersion{
			{80, 0},
			{100, 1},
			{120, 2},
		},
	}

	check(t, expected, "StructVersions", s)
}
