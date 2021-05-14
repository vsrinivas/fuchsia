// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package summarize

import (
	"io"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgentest"
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

type summaryTestCase struct {
	name     string
	fidl     string
	dep      string
	expected string
}

func TestWrite(t *testing.T) {
	tests := []summaryTestCase{
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
			expected: `const l/ENABLED_FLAG bool true
const l/OFFSET int8 -33
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
			expected: `const l/ENABLED_FLAG bool true
const l/OFFSET int8 -33
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
			expected: `const l/ANSWER uint16 42
const l/ANSWER_IN_BINARY uint16 42
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
			expected: `const l/ANSWER uint16 42
const l/ANSWER_IN_BINARY uint16 42
const l/CONVERSION_FACTOR float64 1.41421
const l/DIAMOND uint64 1746410393481133080
const l/ENABLED_FLAG bool true
const l/FUCHSIA uint64 4054509061583223046
const l/MIN_TEMP float32 -273.15
const l/OFFSET int8 -33
const l/POPULATION_USA_2018 uint32 330000000
const l/USERNAME string "squeenze"
library l
`,
		},
		{
			name: "primitives 5, binary operator",
			fidl: `
library l;
const uint8 FOO = 1;
const uint8 BAR = 2;
const uint8 BAZ = FOO | BAR;
`,
			expected: `const l/BAR uint8 2
const l/BAZ uint8 3
const l/FOO uint8 1
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
			expected: `bits/member l/Bits1.BIT1 1
bits/member l/Bits1.BIT2 2
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
			expected: `bits/member l/Bits1.BIT1 1
bits/member l/Bits1.BIT2 2
strict bits l/Bits1 uint32
bits/member l/Bits2.BIT1 1
bits/member l/Bits2.BIT2 2
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
			expected: `bits/member l/Bits.BIT1 1
bits/member l/Bits.BIT2 2
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
			expected: `enum/member l/Beverage.COFFEE 1
enum/member l/Beverage.TEA 2
enum/member l/Beverage.WATER 0
enum/member l/Beverage.WHISKEY 3
flexible enum l/Beverage uint8
enum/member l/Vessel.BOWL 1
enum/member l/Vessel.CUP 0
enum/member l/Vessel.JUG 3
enum/member l/Vessel.TUREEN 2
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
			name: "struct with an element with default value",
			fidl: `
library l;
const string VALUE = "booyah!";
struct S {
  float32 x = 0.314159;
  string foo = "huzzah";
  bool bar = true;
  string baz = VALUE;
};
`,
			expected: `struct/member l/S.bar bool true
struct/member l/S.baz string "booyah!"
struct/member l/S.foo string "huzzah"
struct/member l/S.x float32 0.314159
struct l/S
const l/VALUE string "booyah!"
library l
`,
		},
		{
			name: "struct with an element with default value",
			fidl: `
library l;
const string VALUE = "booyah!";
struct S {
  float32 x = 0.314159;
  string foo = "huzzah";
  bool bar = true;
  string baz = VALUE;
};
`,
			expected: `struct/member l/S.bar bool true
struct/member l/S.baz string "booyah!"
struct/member l/S.foo string "huzzah"
struct/member l/S.x float32 0.314159
struct l/S
const l/VALUE string "booyah!"
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
			expected: `struct/member l/Arrays.form array<float32,16>
struct/member l/Arrays.matrix array<array<string,4>,10>
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
			expected: `struct/member l/Document.description string:optional
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
struct/member l/Vectors.complex vector<vector<array<float32,16>>>
struct/member l/Vectors.nullable_vector_of_strings vector<string>:<24,optional>
struct/member l/Vectors.params vector<int32>:10
struct/member l/Vectors.vector_of_nullable_strings vector<string:optional>
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
    zx.handle h;
    zx.handle:CHANNEL? c;
};
`,
			expected: `struct/member l/Handles.c zx/handle:<CHANNEL,optional>
struct/member l/Handles.h zx/handle
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
struct/member l/Circle.color box<l/Color>
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
strict union l/Either
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
protocol/member l/P.M(box<l/Bar> b) -> (l/Foo c)
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
protocol/member l/P2.M1(client_end:l/P a)
protocol/member l/P2.M2(client_end:<l/P,optional> a)
protocol/member l/P2.M3(server_end:l/P a)
protocol/member l/P2.M4(server_end:<l/P,optional> a)
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
strict union l/P_F3_Result
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
struct/member l/S.f3 string:<4,optional>
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
		{
			name: "reserved keyword",
			fidl: `
library l;
union E {
1: reserved;
2: int32 e;
};
table T {
1: reserved;
2: int32 e;
};
`,
			expected: `union/member l/E.e int32
strict union l/E
table/member l/T.e int32
table l/T
library l
`,
		},
	}
	runWriteTests(t, tests, Write)
}

func TestWriteJSON(t *testing.T) {
	tests := []summaryTestCase{
		{
			name: "library only",
			fidl: `library l;`,
			expected: `[
  {
    "name": "l",
    "kind": "library"
  }
]
`,
		},
		{
			name: "primitives 1",
			fidl: `
library l;
const int8 OFFSET = -33;
const bool ENABLED_FLAG = true;
`,
			expected: `[
  {
    "name": "l/ENABLED_FLAG",
    "kind": "const",
    "declaration": "bool",
    "value": "true"
  },
  {
    "name": "l/OFFSET",
    "kind": "const",
    "declaration": "int8",
    "value": "-33"
  },
  {
    "name": "l",
    "kind": "library"
  }
]
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
			expected: `[
  {
    "name": "l/Bits1.BIT1",
    "kind": "bits/member",
    "value": "1"
  },
  {
    "name": "l/Bits1.BIT2",
    "kind": "bits/member",
    "value": "2"
  },
  {
    "name": "l/Bits1",
    "kind": "bits",
    "declaration": "uint32",
    "strictness": "strict"
  },
  {
    "name": "l",
    "kind": "library"
  }
]
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
			expected: `[
  {
    "name": "l/Bits1.BIT1",
    "kind": "bits/member",
    "value": "1"
  },
  {
    "name": "l/Bits1.BIT2",
    "kind": "bits/member",
    "value": "2"
  },
  {
    "name": "l/Bits1",
    "kind": "bits",
    "declaration": "uint32",
    "strictness": "strict"
  },
  {
    "name": "l/Bits2.BIT1",
    "kind": "bits/member",
    "value": "1"
  },
  {
    "name": "l/Bits2.BIT2",
    "kind": "bits/member",
    "value": "2"
  },
  {
    "name": "l/Bits2",
    "kind": "bits",
    "declaration": "uint32",
    "strictness": "strict"
  },
  {
    "name": "l",
    "kind": "library"
  }
]
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
			expected: `[
  {
    "name": "l/Bits.BIT1",
    "kind": "bits/member",
    "value": "1"
  },
  {
    "name": "l/Bits.BIT2",
    "kind": "bits/member",
    "value": "2"
  },
  {
    "name": "l/Bits",
    "kind": "bits",
    "declaration": "uint8",
    "strictness": "flexible"
  },
  {
    "name": "l",
    "kind": "library"
  }
]
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
			expected: `[
  {
    "name": "l/Beverage.COFFEE",
    "kind": "enum/member",
    "value": "1"
  },
  {
    "name": "l/Beverage.TEA",
    "kind": "enum/member",
    "value": "2"
  },
  {
    "name": "l/Beverage.WATER",
    "kind": "enum/member",
    "value": "0"
  },
  {
    "name": "l/Beverage.WHISKEY",
    "kind": "enum/member",
    "value": "3"
  },
  {
    "name": "l/Beverage",
    "kind": "enum",
    "declaration": "uint8",
    "strictness": "flexible"
  },
  {
    "name": "l/Vessel.BOWL",
    "kind": "enum/member",
    "value": "1"
  },
  {
    "name": "l/Vessel.CUP",
    "kind": "enum/member",
    "value": "0"
  },
  {
    "name": "l/Vessel.JUG",
    "kind": "enum/member",
    "value": "3"
  },
  {
    "name": "l/Vessel.TUREEN",
    "kind": "enum/member",
    "value": "2"
  },
  {
    "name": "l/Vessel",
    "kind": "enum",
    "declaration": "uint32",
    "strictness": "strict"
  },
  {
    "name": "l",
    "kind": "library"
  }
]
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
			expected: `[
  {
    "name": "l/S.x",
    "kind": "struct/member",
    "declaration": "float32"
  },
  {
    "name": "l/S",
    "kind": "struct"
  },
  {
    "name": "l",
    "kind": "library"
  }
]
`,
		},
		{
			name: "struct with an element with default value",
			fidl: `
library l;
const string VALUE = "booyah!";
struct S {
  float32 x = 0.314159;
  string foo = "huzzah";
  bool bar = true;
  string baz = VALUE;
};
`,
			expected: `[
  {
    "name": "l/S.bar",
    "kind": "struct/member",
    "declaration": "bool",
    "value": "true"
  },
  {
    "name": "l/S.baz",
    "kind": "struct/member",
    "declaration": "string",
    "value": "booyah!"
  },
  {
    "name": "l/S.foo",
    "kind": "struct/member",
    "declaration": "string",
    "value": "huzzah"
  },
  {
    "name": "l/S.x",
    "kind": "struct/member",
    "declaration": "float32",
    "value": "0.314159"
  },
  {
    "name": "l/S",
    "kind": "struct"
  },
  {
    "name": "l/VALUE",
    "kind": "const",
    "declaration": "string",
    "value": "booyah!"
  },
  {
    "name": "l",
    "kind": "library"
  }
]
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
			expected: `[
  {
    "name": "l/Arrays.form",
    "kind": "struct/member",
    "declaration": "array<float32,16>"
  },
  {
    "name": "l/Arrays.matrix",
    "kind": "struct/member",
    "declaration": "array<array<string,4>,10>"
  },
  {
    "name": "l/Arrays",
    "kind": "struct"
  },
  {
    "name": "l",
    "kind": "library"
  }
]
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
			expected: `[
  {
    "name": "l/Document.description",
    "kind": "struct/member",
    "declaration": "string:optional"
  },
  {
    "name": "l/Document.title",
    "kind": "struct/member",
    "declaration": "string:40"
  },
  {
    "name": "l/Document",
    "kind": "struct"
  },
  {
    "name": "l",
    "kind": "library"
  }
]
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
			expected: `[
  {
    "name": "l/Vectors.blob",
    "kind": "struct/member",
    "declaration": "vector<uint8>"
  },
  {
    "name": "l/Vectors.complex",
    "kind": "struct/member",
    "declaration": "vector<vector<array<float32,16>>>"
  },
  {
    "name": "l/Vectors.nullable_vector_of_strings",
    "kind": "struct/member",
    "declaration": "vector<string>:<24,optional>"
  },
  {
    "name": "l/Vectors.params",
    "kind": "struct/member",
    "declaration": "vector<int32>:10"
  },
  {
    "name": "l/Vectors.vector_of_nullable_strings",
    "kind": "struct/member",
    "declaration": "vector<string:optional>"
  },
  {
    "name": "l/Vectors",
    "kind": "struct"
  },
  {
    "name": "l",
    "kind": "library"
  }
]
`,
		},
		{
			name: "handles",
			dep:  zxLibrary,
			fidl: `
library l;
using zx;
resource struct Handles {
    zx.handle h;
    zx.handle:CHANNEL? c;
};
`,
			expected: `[
  {
    "name": "l/Handles.c",
    "kind": "struct/member",
    "declaration": "zx/handle:<CHANNEL,optional>"
  },
  {
    "name": "l/Handles.h",
    "kind": "struct/member",
    "declaration": "zx/handle"
  },
  {
    "name": "l/Handles",
    "kind": "struct",
    "resourceness": "resource"
  },
  {
    "name": "l",
    "kind": "library"
  }
]
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
			expected: `[
  {
    "name": "l/A",
    "kind": "struct"
  },
  {
    "name": "l/B.a",
    "kind": "struct/member",
    "declaration": "l/A"
  },
  {
    "name": "l/B",
    "kind": "struct"
  },
  {
    "name": "l",
    "kind": "library"
  }
]
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
			expected: `[
  {
    "name": "l/Circle.center",
    "kind": "struct/member",
    "declaration": "l/CirclePoint"
  },
  {
    "name": "l/Circle.color",
    "kind": "struct/member",
    "declaration": "box<l/Color>"
  },
  {
    "name": "l/Circle.dashed",
    "kind": "struct/member",
    "declaration": "bool"
  },
  {
    "name": "l/Circle.filled",
    "kind": "struct/member",
    "declaration": "bool"
  },
  {
    "name": "l/Circle.radius",
    "kind": "struct/member",
    "declaration": "float32"
  },
  {
    "name": "l/Circle",
    "kind": "struct"
  },
  {
    "name": "l/CirclePoint.x",
    "kind": "struct/member",
    "declaration": "float32"
  },
  {
    "name": "l/CirclePoint.y",
    "kind": "struct/member",
    "declaration": "float32"
  },
  {
    "name": "l/CirclePoint",
    "kind": "struct"
  },
  {
    "name": "l/Color.b",
    "kind": "struct/member",
    "declaration": "float32"
  },
  {
    "name": "l/Color.g",
    "kind": "struct/member",
    "declaration": "float32"
  },
  {
    "name": "l/Color.r",
    "kind": "struct/member",
    "declaration": "float32"
  },
  {
    "name": "l/Color",
    "kind": "struct"
  },
  {
    "name": "l",
    "kind": "library"
  }
]
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
			expected: `[
  {
    "name": "l/Profile.calendars",
    "kind": "table/member",
    "declaration": "vector<string>"
  },
  {
    "name": "l/Profile.locales",
    "kind": "table/member",
    "declaration": "vector<string>"
  },
  {
    "name": "l/Profile.time_zones",
    "kind": "table/member",
    "declaration": "vector<string>"
  },
  {
    "name": "l/Profile",
    "kind": "table"
  },
  {
    "name": "l",
    "kind": "library"
  }
]
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
			expected: `[
  {
    "name": "l/Either.left",
    "kind": "union/member",
    "declaration": "l/Left"
  },
  {
    "name": "l/Either.right",
    "kind": "union/member",
    "declaration": "l/Right"
  },
  {
    "name": "l/Either",
    "kind": "union",
    "strictness": "strict"
  },
  {
    "name": "l/Left",
    "kind": "struct"
  },
  {
    "name": "l/Right",
    "kind": "struct"
  },
  {
    "name": "l",
    "kind": "library"
  }
]
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
			expected: `[
  {
    "name": "l/Calculator.Add",
    "kind": "protocol/member",
    "declaration": "(int32 a,int32 b) -> (int32 sum)"
  },
  {
    "name": "l/Calculator",
    "kind": "protocol"
  },
  {
    "name": "l",
    "kind": "library"
  }
]
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
			expected: `[
  {
    "name": "l/Bar",
    "kind": "struct"
  },
  {
    "name": "l/Foo",
    "kind": "struct"
  },
  {
    "name": "l/P.M",
    "kind": "protocol/member",
    "declaration": "(box<l/Bar> b) -> (l/Foo c)"
  },
  {
    "name": "l/P",
    "kind": "protocol"
  },
  {
    "name": "l",
    "kind": "library"
  }
]
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
			expected: `[
  {
    "name": "l/Bar",
    "kind": "struct"
  },
  {
    "name": "l/P",
    "kind": "protocol"
  },
  {
    "name": "l/P2.M1",
    "kind": "protocol/member",
    "declaration": "(client_end:l/P a)"
  },
  {
    "name": "l/P2.M2",
    "kind": "protocol/member",
    "declaration": "(client_end:<l/P,optional> a)"
  },
  {
    "name": "l/P2.M3",
    "kind": "protocol/member",
    "declaration": "(server_end:l/P a)"
  },
  {
    "name": "l/P2.M4",
    "kind": "protocol/member",
    "declaration": "(server_end:<l/P,optional> a)"
  },
  {
    "name": "l/P2",
    "kind": "protocol"
  },
  {
    "name": "l",
    "kind": "library"
  }
]
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
			expected: `[
  {
    "name": "l/P.F1",
    "kind": "protocol/member",
    "declaration": " -> (int32 a)"
  },
  {
    "name": "l/P.F2",
    "kind": "protocol/member",
    "declaration": "() -> (int32 a)"
  },
  {
    "name": "l/P.F3",
    "kind": "protocol/member",
    "declaration": "() -> (l/P_F3_Result result)"
  },
  {
    "name": "l/P.F4",
    "kind": "protocol/member",
    "declaration": "()"
  },
  {
    "name": "l/P",
    "kind": "protocol"
  },
  {
    "name": "l/P_F3_Response",
    "kind": "struct"
  },
  {
    "name": "l/P_F3_Result.err",
    "kind": "union/member",
    "declaration": "int32"
  },
  {
    "name": "l/P_F3_Result.response",
    "kind": "union/member",
    "declaration": "l/P_F3_Response"
  },
  {
    "name": "l/P_F3_Result",
    "kind": "union",
    "strictness": "strict"
  },
  {
    "name": "l",
    "kind": "library"
  }
]
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
			expected: `[
  {
    "name": "l/S.f1",
    "kind": "struct/member",
    "declaration": "string"
  },
  {
    "name": "l/S.f2",
    "kind": "struct/member",
    "declaration": "string:4"
  },
  {
    "name": "l/S.f3",
    "kind": "struct/member",
    "declaration": "string:<4,optional>"
  },
  {
    "name": "l/S",
    "kind": "struct"
  },
  {
    "name": "l",
    "kind": "library"
  }
]
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
			expected: `[
  {
    "name": "l/A.a",
    "kind": "struct/member",
    "declaration": "l2/T"
  },
  {
    "name": "l/A",
    "kind": "struct"
  },
  {
    "name": "l",
    "kind": "library"
  }
]
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
			expected: `[
  {
    "name": "l/Bar",
    "kind": "struct"
  },
  {
    "name": "l/Calculator.Add",
    "kind": "protocol/member",
    "declaration": "(l2/T a,l/Bar b) -> (l/Foo c)"
  },
  {
    "name": "l/Calculator",
    "kind": "protocol"
  },
  {
    "name": "l/Foo",
    "kind": "struct"
  },
  {
    "name": "l",
    "kind": "library"
  }
]
`,
		},
		{
			name: "protocol with foreign library 2",
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
			expected: `[
  {
    "name": "l/Bar",
    "kind": "struct"
  },
  {
    "name": "l/Calculator.Add",
    "kind": "protocol/member",
    "declaration": "(l2/T a,l/Bar b) -> (l/Foo c)"
  },
  {
    "name": "l/Calculator",
    "kind": "protocol"
  },
  {
    "name": "l/Foo",
    "kind": "struct"
  },
  {
    "name": "l",
    "kind": "library"
  }
]
`,
		},
		{
			name: "reserved keyword",
			fidl: `
library l;
union E {
1: reserved;
2: int32 e;
};
table T {
1: reserved;
2: int32 e;
};
`,
			expected: `[
  {
    "name": "l/E.e",
    "kind": "union/member",
    "declaration": "int32"
  },
  {
    "name": "l/E",
    "kind": "union",
    "strictness": "strict"
  },
  {
    "name": "l/T.e",
    "kind": "table/member",
    "declaration": "int32"
  },
  {
    "name": "l/T",
    "kind": "table"
  },
  {
    "name": "l",
    "kind": "library"
  }
]
`,
		},
	}
	runWriteTests(t, tests, WriteJSON)
}

func runWriteTests(t *testing.T, tests []summaryTestCase, writeFn func(fidlgen.Root, io.Writer) error) {
	t.Helper()
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			c := fidlgentest.EndToEndTest{T: t}
			if test.dep != "" {
				c = c.WithDependency(test.dep)
			}
			r := c.Single(test.fidl)
			var sb strings.Builder
			if err := writeFn(r, &sb); err != nil {
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
