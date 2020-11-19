// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"testing"
)

func assertEqual(t *testing.T, a interface{}, b interface{}) {
	if a != b {
		t.Fatalf("%s != %s", a, b)
	}
}

func assertNotEqual(t *testing.T, a interface{}, b interface{}) {
	if a == b {
		t.Fatalf("%s == %s", a, b)
	}
}

func TestExample(t *testing.T) {
	type color namespacedEnumMember
	type colors struct {
		Red   color
		Green color
		Blue  color
	}
	Colors := namespacedEnum(colors{}).(colors)

	assertEqual(t, Colors.Red, Colors.Red)
	assertNotEqual(t, Colors.Red, Colors.Green)
	assertNotEqual(t, Colors.Red, Colors.Blue)

	assertEqual(t, int(Colors.Red), 1)
	assertEqual(t, int(Colors.Green), 2)
	assertEqual(t, int(Colors.Blue), 3)
}

func assertPanic(t *testing.T) {
	if r := recover(); r == nil {
		t.Errorf("The code did not panic")
	}
}

func TestInvalidNamespace(t *testing.T) {
	defer assertPanic(t)

	namespacedEnum(1)
}

func TestIncompatibleField(t *testing.T) {
	defer assertPanic(t)

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
	defer assertPanic(t)

	type foo namespacedEnumMember
	type bar namespacedEnumMember
	type enums struct {
		Foo foo
		Bar bar
	}

	namespacedEnum(enums{})
}
