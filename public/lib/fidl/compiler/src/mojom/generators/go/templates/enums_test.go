// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

import (
	"testing"

	"mojom/generators/go/translator"
)

func TestEnumDecl(t *testing.T) {
	expected := `type SomeEnum int32

const (
	SomeEnum_Alpha SomeEnum = 0
	SomeEnum_Beta SomeEnum = 10
	SomeEnum_Gamma SomeEnum = 11
)`

	enum := translator.EnumTemplate{
		Name: "SomeEnum",
		Values: []translator.EnumValueTemplate{
			{Name: "SomeEnum_Alpha", Value: 0},
			{Name: "SomeEnum_Beta", Value: 10},
			{Name: "SomeEnum_Gamma", Value: 11},
		},
	}

	check(t, expected, "EnumDecl", enum)
}
