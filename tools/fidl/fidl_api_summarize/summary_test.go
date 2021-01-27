// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// fidl_api_summarize tests package.
package main

import (
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	fidl_testing "go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen_testing"
)

var (
	// zxLibrary is a shortened version of zx_common.fidl, for tests.
	zxLibrary = `
library zx;
enum obj_type : uint32 {
  CHANNEL = 4;
};
resource_definition handle : uint32 {
  properties {
    obj_type subtype;
  };
};
`

	// l2Library is a sample dependency taken in by some tests.
	l2Library = `library l2; struct T{};`
)

func TestSummarize(t *testing.T) {
	tests := []struct {
		name     string
		fidl     string
		dep      string
		expected string
	}{
		{
			name: "library only",
			fidl: `library l;`,
			expected: `library l
`,
		},
		{
			name: "primitives 1",
			fidl: `
library l;
const int8 OFFSET = -33;
const bool ENABLED_FLAG = true;
`,
			expected: `const bool l/ENABLED_FLAG
const int8 l/OFFSET
library l
`,
		},
		{
			// Same as above, except reordered.
			name: "primitives 2",
			fidl: `
library l;
const bool ENABLED_FLAG = true;
const int8 OFFSET = -33;
`,
			expected: `const bool l/ENABLED_FLAG
const int8 l/OFFSET
library l
`,
		},
		{
			name: "primitives 3",
			fidl: `
library l;
const uint16 ANSWER = 42;
const uint16 ANSWER_IN_BINARY = 0b101010;
`,
			expected: `const uint16 l/ANSWER
const uint16 l/ANSWER_IN_BINARY
library l
`,
		},
		{
			name: "primitives 4",
			fidl: `
library l;
const bool ENABLED_FLAG = true;
const int8 OFFSET = -33;
const uint16 ANSWER = 42;
const uint16 ANSWER_IN_BINARY = 0b101010;
const uint32 POPULATION_USA_2018 = 330000000;
const uint64 DIAMOND = 0x183c7effff7e3c18;
const uint64 FUCHSIA = 4054509061583223046;
const string USERNAME = "squeenze";
const float32 MIN_TEMP = -273.15;
const float64 CONVERSION_FACTOR = 1.41421358;
`,
			expected: `const uint16 l/ANSWER
const uint16 l/ANSWER_IN_BINARY
const float64 l/CONVERSION_FACTOR
const uint64 l/DIAMOND
const bool l/ENABLED_FLAG
const uint64 l/FUCHSIA
const float32 l/MIN_TEMP
const int8 l/OFFSET
const uint32 l/POPULATION_USA_2018
const string l/USERNAME
library l
`,
		},
		{
			name: "bits",
			fidl: `
library l;
strict bits Bits1 {
  BIT1 = 0x01;
  BIT2 = 0x02;
};
`,
			expected: `bits/member l/Bits1.BIT1
bits/member l/Bits1.BIT2
strict bits l/Bits1 uint32
library l
`,
		},
		{
			name: "bits 2",
			fidl: `
library l;
strict bits Bits1 {
  BIT1 = 0x01;
  BIT2 = 0x02;
};
strict bits Bits2 {
  BIT1 = 0x01;
  BIT2 = 0x02;
};
`,
			expected: `bits/member l/Bits1.BIT1
bits/member l/Bits1.BIT2
strict bits l/Bits1 uint32
bits/member l/Bits2.BIT1
bits/member l/Bits2.BIT2
strict bits l/Bits2 uint32
library l
`,
		},
		{
			name: "bits 3",
			fidl: `
library l;
flexible bits Bits : uint8 {
  BIT1 = 0x01;
  BIT2 = 0x02;
};
`,
			expected: `bits/member l/Bits.BIT1
bits/member l/Bits.BIT2
flexible bits l/Bits uint8
library l
`,
		},
		{
			name: "enums",
			fidl: `
			library l;
flexible enum Beverage : uint8 {
    WATER = 0;
    COFFEE = 1;
    TEA = 2;
    WHISKEY = 3;
};

// Underlying type is assumed to be uint32.
strict enum Vessel {
    CUP = 0;
    BOWL = 1;
    TUREEN = 2;
    JUG = 3;
};
`,
			expected: `enum/member l/Beverage.COFFEE
enum/member l/Beverage.TEA
enum/member l/Beverage.WATER
enum/member l/Beverage.WHISKEY
flexible enum l/Beverage uint8
enum/member l/Vessel.BOWL
enum/member l/Vessel.CUP
enum/member l/Vessel.JUG
enum/member l/Vessel.TUREEN
strict enum l/Vessel uint32
library l
`,
		},
		{
			name: "struct as precondition for arrays",
			fidl: `
library l;
struct S {
  float32 x;
};
`,
			expected: `struct/member l/S.x float32
struct l/S
library l
`,
		},
		{
			name: "arrays",
			fidl: `
library l;
struct Arrays {
    array<float32>:16 form;
    array<array<string>:4>:10 matrix;
};
`,
			expected: `struct/member l/Arrays.form array<float32>:16
struct/member l/Arrays.matrix array<array<string>:4>:10
struct l/Arrays
library l
`,
		},
		{
			name: "strings",
			fidl: `
library l;
struct Document {
    string:40 title;
    string? description;
};
`,
			expected: `struct/member l/Document.description string?
struct/member l/Document.title string:40
struct l/Document
library l
`,
		},
		{
			name: "vectors",
			fidl: `
library l;
struct Vectors {
    vector<int32>:10 params;
    bytes blob;
    vector<string>:24? nullable_vector_of_strings;
    vector<string?> vector_of_nullable_strings;
    vector<vector<array<float32>:16>> complex;
};
`,
			expected: `struct/member l/Vectors.blob vector<uint8>
struct/member l/Vectors.complex vector<vector<array<float32>:16>>
struct/member l/Vectors.nullable_vector_of_strings vector<string>:24?
struct/member l/Vectors.params vector<int32>:10
struct/member l/Vectors.vector_of_nullable_strings vector<string?>
struct l/Vectors
library l
`,
		},
		{
			name: "handles",
			dep:  zxLibrary,
			fidl: `
library l;
using zx;
resource struct Handles {
    // TODO(fxbug.dev/51001): Remove built-in handles.
    handle h;
    zx.handle:CHANNEL? c;
};
`,
			expected: `struct/member l/Handles.c zx/handle:zx/obj_type.CHANNEL?
struct/member l/Handles.h handle
resource struct l/Handles
library l
`,
		},
		{
			name: "struct local type reference",
			fidl: `
library l;
struct A {};
struct B {
	A a;
};
`,
			expected: `struct l/A
struct/member l/B.a l/A
struct l/B
library l
`,
		},
		{
			name: "structs 2",
			fidl: `
library l;
struct CirclePoint {
    float32 x;
    float32 y;
};
struct Color {
    float32 r;
    float32 g;
    float32 b;
};
struct Circle {
    bool filled;
    CirclePoint center;
    float32 radius;
    Color? color;
    bool dashed;
};
`,
			expected: `struct/member l/Circle.center l/CirclePoint
struct/member l/Circle.color l/Color?
struct/member l/Circle.dashed bool
struct/member l/Circle.filled bool
struct/member l/Circle.radius float32
struct l/Circle
struct/member l/CirclePoint.x float32
struct/member l/CirclePoint.y float32
struct l/CirclePoint
struct/member l/Color.b float32
struct/member l/Color.g float32
struct/member l/Color.r float32
struct l/Color
library l
`,
		},
		{
			name: "tables",
			fidl: `
library l;
table Profile {
    1: vector<string> locales;
    2: vector<string> calendars;
    3: vector<string> time_zones;
};
`,
			expected: `table/member l/Profile.calendars vector<string>
table/member l/Profile.locales vector<string>
table/member l/Profile.time_zones vector<string>
table l/Profile
library l
`,
		},
		{
			name: "unions",
			fidl: `
library l;
struct Left {};
struct Right {};
union Either {
    1: Left left;
    2: Right right;
};
`,
			expected: `union/member l/Either.left l/Left
union/member l/Either.right l/Right
union l/Either
struct l/Left
struct l/Right
library l
`,
		},
		{
			name: "protocols 1",
			fidl: `
library l;
protocol Calculator {
    Add(int32 a, int32 b) -> (int32 sum);
};
`,
			expected: `protocol/member l/Calculator.Add(int32 a,int32 b) -> (int32 sum)
protocol l/Calculator
library l
`,
		},
		{
			name: "protocols 2",
			fidl: `
library l;
struct Foo {};
struct Bar {};
protocol P {
    M(Bar? b) -> (Foo c);
};
`,
			expected: `struct l/Bar
struct l/Foo
protocol/member l/P.M(l/Bar? b) -> (l/Foo c)
protocol l/P
library l
`,
		},
		{
			name: "protocols 3",
			fidl: `
library l;
struct Bar {};
protocol P {};
protocol P2 {
    M1(P a);
    M2(P? a);
    M3(request<P> a);
    M4(request<P>? a);
};
`,
			expected: `struct l/Bar
protocol l/P
protocol/member l/P2.M1(l/P a)
protocol/member l/P2.M2(l/P? a)
protocol/member l/P2.M3(request<l/P> a)
protocol/member l/P2.M4(request<l/P>? a)
protocol l/P2
library l
`,
		},
		{
			name: "protocols 4",
			fidl: `
library l;
protocol P {
    -> F1(int32 a);
    F2() -> (int32 a);
	F3() -> () error int32;
	F4();
};
`,
			expected: `protocol/member l/P.F1 -> (int32 a)
protocol/member l/P.F2() -> (int32 a)
protocol/member l/P.F3() -> (l/P_F3_Result result)
protocol/member l/P.F4()
protocol l/P
struct l/P_F3_Response
union/member l/P_F3_Result.err int32
union/member l/P_F3_Result.response l/P_F3_Response
union l/P_F3_Result
library l
`,
		},
		{
			name: "check types",
			fidl: `
library l;
struct S {
   string f1;
   string:4 f2;
   string:4? f3;
};
`,
			expected: `struct/member l/S.f1 string
struct/member l/S.f2 string:4
struct/member l/S.f3 string:4?
struct l/S
library l
`,
		},
		{
			name: "with foreign library",
			dep:  l2Library,
			fidl: `
library l;
using l2;
struct A {
  l2.T a;
};
`,
			expected: `struct/member l/A.a l2/T
struct l/A
library l
`,
		},
		{
			name: "protocol with foreign library",
			dep:  l2Library,
			fidl: `
library l;
using l2;
struct Foo {};
struct Bar {};
protocol Calculator {
    Add(l2.T a, Bar b) -> (Foo c);
};
`,
			expected: `struct l/Bar
protocol/member l/Calculator.Add(l2/T a,l/Bar b) -> (l/Foo c)
protocol l/Calculator
struct l/Foo
library l
`,
		},
		{
			name: "protocol with foreign library",
			dep:  l2Library,
			fidl: `
library l;
using l2;
struct Foo {};
struct Bar {};
protocol Calculator {
    Add(l2.T a, Bar b) -> (Foo c);
};
`,
			expected: `struct l/Bar
protocol/member l/Calculator.Add(l2/T a,l/Bar b) -> (l/Foo c)
protocol l/Calculator
struct l/Foo
library l
`,
		},
	}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			c := fidl_testing.EndToEndTest{T: t}
			if test.dep != "" {
				c = c.WithDependency(test.dep)
			}
			r := c.Single(test.fidl)
			var sb strings.Builder
			if err := Summarize(r, &sb); err != nil {
				t.Fatalf("while summarizing file: %v", err)
			}
			actual := strings.Split(sb.String(), "\n")
			expected := strings.Split(test.expected, "\n")

			if !cmp.Equal(expected, actual) {
				t.Errorf("expected:\n---BEGIN---\n%+v\n---END---\n\n"+
					"actual:\n---BEGIN---\n%+v\n---END---\n\ndiff:\n%v\n\nroot: %+v",
					test.expected, sb.String(),
					cmp.Diff(expected, actual), r)
			}
		})
	}
}
