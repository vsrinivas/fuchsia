// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"testing"
)

func TestExample(t *testing.T) {
	type color namespacedEnumMember
	type colors struct {
		Red   color
		Green color
		Blue  color
	}
	Colors := namespacedEnum(colors{}).(colors)

	expectEqual(t, Colors.Red, Colors.Red)
	expectNotEqual(t, Colors.Red, Colors.Green)
	expectNotEqual(t, Colors.Red, Colors.Blue)

	expectEqual(t, int(Colors.Red), 1)
	expectEqual(t, int(Colors.Green), 2)
	expectEqual(t, int(Colors.Blue), 3)
}

func TestInvalidNamespace(t *testing.T) {
	defer assertPanicOccurs(t)

	namespacedEnum(1)
}

func TestIncompatibleField(t *testing.T) {
	defer assertPanicOccurs(t)

	type foo namespacedEnumMember
	type bar namespacedEnumMember
	type enums struct {
		Foo       foo
		Bar       bar
		unrelated int
	}

	namespacedEnum(enums{})
}

func TestDifferingFieldTypes(t *testing.T) {
	defer assertPanicOccurs(t)

	type foo namespacedEnumMember
	type bar namespacedEnumMember
	type enums struct {
		Foo foo
		Bar bar
	}

	namespacedEnum(enums{})
}
