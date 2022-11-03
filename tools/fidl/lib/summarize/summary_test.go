// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package summarize

import (
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgentest"
)

var (
	// zxLibrary is a shortened version of zx_common.fidl, for tests.
	zxLibrary = `
library zx;
type obj_type = enum : uint32 {
  CHANNEL = 4;
};
resource_definition handle : uint32 {
  properties {
    subtype obj_type;
  };
};
`

	// l2Library is a sample dependency taken in by some tests.
	l2Library = `
library l2;
type T = struct {};
type UnaryArg = struct {
    num int32;
};
protocol Inverter {
    Invert(UnaryArg) -> (UnaryArg);
};
`
)

type summaryTestCase struct {
	name     string
	fidl     string
	dep      string
	expected string
}

func TestJSONSummaryFormat(t *testing.T) {
	tests := []summaryTestCase{
		{
			name: "library only",
			fidl: `library l;`,
			expected: `[
    {
        "kind": "library",
        "name": "l"
    }
]
`,
		},
		{
			name: "primitives 1",
			fidl: `
library l;
const OFFSET int8 = -33;
const ENABLED_FLAG bool = true;
`,
			expected: `[
    {
        "kind": "const",
        "name": "l/ENABLED_FLAG",
        "type": "bool",
        "value": "true"
    },
    {
        "kind": "const",
        "name": "l/OFFSET",
        "type": "int8",
        "value": "-33"
    },
    {
        "kind": "library",
        "name": "l"
    }
]
`,
		},
		{
			// Same as above, except reordered.
			name: "primitives 2",
			fidl: `
library l;
const ENABLED_FLAG bool = true;
const OFFSET int8 = -33;
`,
			expected: `[
    {
        "kind": "const",
        "name": "l/ENABLED_FLAG",
        "type": "bool",
        "value": "true"
    },
    {
        "kind": "const",
        "name": "l/OFFSET",
        "type": "int8",
        "value": "-33"
    },
    {
        "kind": "library",
        "name": "l"
    }
]
`,
		},
		{
			name: "primitives 3",
			fidl: `
library l;
const ANSWER uint16 = 42;
const ANSWER_IN_BINARY uint16 = 0b101010;
`,
			expected: `[
    {
        "kind": "const",
        "name": "l/ANSWER",
        "type": "uint16",
        "value": "42"
    },
    {
        "kind": "const",
        "name": "l/ANSWER_IN_BINARY",
        "type": "uint16",
        "value": "42"
    },
    {
        "kind": "library",
        "name": "l"
    }
]
`,
		},
		{
			name: "primitives 4",
			fidl: `
library l;
const ENABLED_FLAG bool = true;
const OFFSET int8 = -33;
const ANSWER uint16 = 42;
const ANSWER_IN_BINARY uint16 = 0b101010;
const POPULATION_USA_2018 uint32 = 330000000;
const DIAMOND uint64 = 0x183c7effff7e3c18;
const FUCHSIA uint64 = 4054509061583223046;
const USERNAME string = "squeenze";
const MIN_TEMP float32 = -273.15;
const CONVERSION_FACTOR float64 = 1.41421358;
`,
			expected: `[
    {
        "kind": "const",
        "name": "l/ANSWER",
        "type": "uint16",
        "value": "42"
    },
    {
        "kind": "const",
        "name": "l/ANSWER_IN_BINARY",
        "type": "uint16",
        "value": "42"
    },
    {
        "kind": "const",
        "name": "l/CONVERSION_FACTOR",
        "type": "float64",
        "value": "1.41421"
    },
    {
        "kind": "const",
        "name": "l/DIAMOND",
        "type": "uint64",
        "value": "1746410393481133080"
    },
    {
        "kind": "const",
        "name": "l/ENABLED_FLAG",
        "type": "bool",
        "value": "true"
    },
    {
        "kind": "const",
        "name": "l/FUCHSIA",
        "type": "uint64",
        "value": "4054509061583223046"
    },
    {
        "kind": "const",
        "name": "l/MIN_TEMP",
        "type": "float32",
        "value": "-273.15"
    },
    {
        "kind": "const",
        "name": "l/OFFSET",
        "type": "int8",
        "value": "-33"
    },
    {
        "kind": "const",
        "name": "l/POPULATION_USA_2018",
        "type": "uint32",
        "value": "330000000"
    },
    {
        "kind": "const",
        "name": "l/USERNAME",
        "type": "string",
        "value": "squeenze"
    },
    {
        "kind": "library",
        "name": "l"
    }
]
`,
		},
		{
			name: "primitives 5, binary operator",
			fidl: `
library l;
const FOO uint8 = 1;
const BAR uint8 = 2;
const BAZ uint8 = FOO | BAR;
`,
			expected: `[
    {
        "kind": "const",
        "name": "l/BAR",
        "type": "uint8",
        "value": "2"
    },
    {
        "kind": "const",
        "name": "l/BAZ",
        "type": "uint8",
        "value": "3"
    },
    {
        "kind": "const",
        "name": "l/FOO",
        "type": "uint8",
        "value": "1"
    },
    {
        "kind": "library",
        "name": "l"
    }
]
`,
		},
		{
			name: "bits",
			fidl: `
library l;
type Bits1 = strict bits {
    BIT1 = 0x01;
    BIT2 = 0x02;
};
`,
			expected: `[
    {
        "kind": "bits/member",
        "name": "l/Bits1.BIT1",
        "value": "1"
    },
    {
        "kind": "bits/member",
        "name": "l/Bits1.BIT2",
        "value": "2"
    },
    {
        "kind": "bits",
        "name": "l/Bits1",
        "strictness": "strict",
        "type": "uint32"
    },
    {
        "kind": "library",
        "name": "l"
    }
]
`,
		},
		{
			name: "bits 2",
			fidl: `
library l;
type Bits1 = strict bits {
    BIT1 = 0x01;
    BIT2 = 0x02;
};
type Bits2 = strict bits {
    BIT1 = 0x01;
    BIT2 = 0x02;
};
`,
			expected: `[
    {
        "kind": "bits/member",
        "name": "l/Bits1.BIT1",
        "value": "1"
    },
    {
        "kind": "bits/member",
        "name": "l/Bits1.BIT2",
        "value": "2"
    },
    {
        "kind": "bits",
        "name": "l/Bits1",
        "strictness": "strict",
        "type": "uint32"
    },
    {
        "kind": "bits/member",
        "name": "l/Bits2.BIT1",
        "value": "1"
    },
    {
        "kind": "bits/member",
        "name": "l/Bits2.BIT2",
        "value": "2"
    },
    {
        "kind": "bits",
        "name": "l/Bits2",
        "strictness": "strict",
        "type": "uint32"
    },
    {
        "kind": "library",
        "name": "l"
    }
]
`,
		},
		{
			name: "bits 3",
			fidl: `
library l;
type Bits = flexible bits : uint8 {
  BIT1 = 0x01;
  BIT2 = 0x02;
};
`,
			expected: `[
    {
        "kind": "bits/member",
        "name": "l/Bits.BIT1",
        "value": "1"
    },
    {
        "kind": "bits/member",
        "name": "l/Bits.BIT2",
        "value": "2"
    },
    {
        "kind": "bits",
        "name": "l/Bits",
        "strictness": "flexible",
        "type": "uint8"
    },
    {
        "kind": "library",
        "name": "l"
    }
]
`,
		},
		{
			name: "enums",
			fidl: `
library l;
type Beverage = flexible enum : uint8 {
    WATER = 0;
    COFFEE = 1;
    TEA = 2;
    WHISKEY = 3;
};

// Underlying type is assumed to be uint32.
type Vessel = strict enum {
    CUP = 0;
    BOWL = 1;
    TUREEN = 2;
    JUG = 3;
};
`,
			expected: `[
    {
        "kind": "enum/member",
        "name": "l/Beverage.COFFEE",
        "value": "1"
    },
    {
        "kind": "enum/member",
        "name": "l/Beverage.TEA",
        "value": "2"
    },
    {
        "kind": "enum/member",
        "name": "l/Beverage.WATER",
        "value": "0"
    },
    {
        "kind": "enum/member",
        "name": "l/Beverage.WHISKEY",
        "value": "3"
    },
    {
        "kind": "enum",
        "name": "l/Beverage",
        "strictness": "flexible",
        "type": "uint8"
    },
    {
        "kind": "enum/member",
        "name": "l/Vessel.BOWL",
        "value": "1"
    },
    {
        "kind": "enum/member",
        "name": "l/Vessel.CUP",
        "value": "0"
    },
    {
        "kind": "enum/member",
        "name": "l/Vessel.JUG",
        "value": "3"
    },
    {
        "kind": "enum/member",
        "name": "l/Vessel.TUREEN",
        "value": "2"
    },
    {
        "kind": "enum",
        "name": "l/Vessel",
        "strictness": "strict",
        "type": "uint32"
    },
    {
        "kind": "library",
        "name": "l"
    }
]
`,
		},
		{
			name: "struct as precondition for arrays",
			fidl: `
library l;
type S = struct {
  x float32;
};
`,
			expected: `[
    {
        "kind": "struct/member",
        "name": "l/S.x",
        "ordinal": "1",
        "type": "float32"
    },
    {
        "kind": "struct",
        "name": "l/S"
    },
    {
        "kind": "library",
        "name": "l"
    }
]
`,
		},
		{
			name: "struct with an element with default value",
			fidl: `
library l;
const VALUE string = "booyah!";
type S = struct {
  @allow_deprecated_struct_defaults
  x float32 = 0.314159;
  @allow_deprecated_struct_defaults
  foo string = "huzzah";
  @allow_deprecated_struct_defaults
  bar bool = true;
  @allow_deprecated_struct_defaults
  baz string = VALUE;
};
`,
			expected: `[
    {
        "kind": "struct/member",
        "name": "l/S.bar",
        "ordinal": "3",
        "type": "bool",
        "value": "true"
    },
    {
        "kind": "struct/member",
        "name": "l/S.baz",
        "ordinal": "4",
        "type": "string",
        "value": "booyah!"
    },
    {
        "kind": "struct/member",
        "name": "l/S.foo",
        "ordinal": "2",
        "type": "string",
        "value": "huzzah"
    },
    {
        "kind": "struct/member",
        "name": "l/S.x",
        "ordinal": "1",
        "type": "float32",
        "value": "0.314159"
    },
    {
        "kind": "struct",
        "name": "l/S"
    },
    {
        "kind": "const",
        "name": "l/VALUE",
        "type": "string",
        "value": "booyah!"
    },
    {
        "kind": "library",
        "name": "l"
    }
]
`,
		},
		{
			name: "arrays",
			fidl: `
library l;
type Arrays = struct {
    form array<float32, 16>;
    matrix array<array<string, 4>, 10>;
};
`,
			expected: `[
    {
        "kind": "struct/member",
        "name": "l/Arrays.form",
        "ordinal": "1",
        "type": "array<float32,16>"
    },
    {
        "kind": "struct/member",
        "name": "l/Arrays.matrix",
        "ordinal": "2",
        "type": "array<array<string,4>,10>"
    },
    {
        "kind": "struct",
        "name": "l/Arrays"
    },
    {
        "kind": "library",
        "name": "l"
    }
]
`,
		},
		{
			name: "strings",
			fidl: `
library l;
type Document = struct {
    title string:40;
    description string:optional;
};
`,
			expected: `[
    {
        "kind": "struct/member",
        "name": "l/Document.description",
        "ordinal": "2",
        "type": "string:optional"
    },
    {
        "kind": "struct/member",
        "name": "l/Document.title",
        "ordinal": "1",
        "type": "string:40"
    },
    {
        "kind": "struct",
        "name": "l/Document"
    },
    {
        "kind": "library",
        "name": "l"
    }
]
`,
		},
		{
			name: "vectors",
			fidl: `
library l;
type Vectors = struct {
    params vector<int32>:10;
    blob vector<uint8>;
    nullable_vector_of_strings vector<string>:<24, optional>;
    vector_of_nullable_strings vector<string:optional>;
    complex vector<vector<array<float32, 16>>>;
};
`,
			expected: `[
    {
        "kind": "struct/member",
        "name": "l/Vectors.blob",
        "ordinal": "2",
        "type": "vector<uint8>"
    },
    {
        "kind": "struct/member",
        "name": "l/Vectors.complex",
        "ordinal": "5",
        "type": "vector<vector<array<float32,16>>>"
    },
    {
        "kind": "struct/member",
        "name": "l/Vectors.nullable_vector_of_strings",
        "ordinal": "3",
        "type": "vector<string>:<24,optional>"
    },
    {
        "kind": "struct/member",
        "name": "l/Vectors.params",
        "ordinal": "1",
        "type": "vector<int32>:10"
    },
    {
        "kind": "struct/member",
        "name": "l/Vectors.vector_of_nullable_strings",
        "ordinal": "4",
        "type": "vector<string:optional>"
    },
    {
        "kind": "struct",
        "name": "l/Vectors"
    },
    {
        "kind": "library",
        "name": "l"
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
type Handles = resource struct {
    h zx.handle;
    c zx.handle:<CHANNEL, optional>;
};
`,
			expected: `[
    {
        "kind": "struct/member",
        "name": "l/Handles.c",
        "ordinal": "2",
        "type": "zx/handle:<CHANNEL,optional>"
    },
    {
        "kind": "struct/member",
        "name": "l/Handles.h",
        "ordinal": "1",
        "type": "zx/handle"
    },
    {
        "kind": "struct",
        "name": "l/Handles",
        "resourceness": "resource"
    },
    {
        "kind": "library",
        "name": "l"
    }
]
`,
		},
		{
			name: "struct local type reference",
			fidl: `
library l;
type A = struct {};
type B = struct {
	a A;
};
`,
			expected: `[
    {
        "kind": "struct",
        "name": "l/A"
    },
    {
        "kind": "struct/member",
        "name": "l/B.a",
        "ordinal": "1",
        "type": "l/A"
    },
    {
        "kind": "struct",
        "name": "l/B"
    },
    {
        "kind": "library",
        "name": "l"
    }
]
`,
		},
		{
			name: "structs 2",
			fidl: `
library l;
type CirclePoint = struct {
    x float32;
    y float32;
};
type Color = struct {
    r float32;
    g float32;
    b float32;
};
type Circle = struct {
    filled bool;
    center CirclePoint;
    radius float32;
    color box<Color>;
    dashed bool;
};
`,
			expected: `[
    {
        "kind": "struct/member",
        "name": "l/Circle.center",
        "ordinal": "2",
        "type": "l/CirclePoint"
    },
    {
        "kind": "struct/member",
        "name": "l/Circle.color",
        "ordinal": "4",
        "type": "box<l/Color>"
    },
    {
        "kind": "struct/member",
        "name": "l/Circle.dashed",
        "ordinal": "5",
        "type": "bool"
    },
    {
        "kind": "struct/member",
        "name": "l/Circle.filled",
        "ordinal": "1",
        "type": "bool"
    },
    {
        "kind": "struct/member",
        "name": "l/Circle.radius",
        "ordinal": "3",
        "type": "float32"
    },
    {
        "kind": "struct",
        "name": "l/Circle"
    },
    {
        "kind": "struct/member",
        "name": "l/CirclePoint.x",
        "ordinal": "1",
        "type": "float32"
    },
    {
        "kind": "struct/member",
        "name": "l/CirclePoint.y",
        "ordinal": "2",
        "type": "float32"
    },
    {
        "kind": "struct",
        "name": "l/CirclePoint"
    },
    {
        "kind": "struct/member",
        "name": "l/Color.b",
        "ordinal": "3",
        "type": "float32"
    },
    {
        "kind": "struct/member",
        "name": "l/Color.g",
        "ordinal": "2",
        "type": "float32"
    },
    {
        "kind": "struct/member",
        "name": "l/Color.r",
        "ordinal": "1",
        "type": "float32"
    },
    {
        "kind": "struct",
        "name": "l/Color"
    },
    {
        "kind": "library",
        "name": "l"
    }
]
`,
		},
		{
			name: "tables",
			fidl: `
library l;
type Profile = table {
    1: locales vector<string>;
    2: calendars vector<string>;
    3: time_zones vector<string>;
};
`,
			expected: `[
    {
        "kind": "table/member",
        "name": "l/Profile.calendars",
        "ordinal": "2",
        "type": "vector<string>"
    },
    {
        "kind": "table/member",
        "name": "l/Profile.locales",
        "ordinal": "1",
        "type": "vector<string>"
    },
    {
        "kind": "table/member",
        "name": "l/Profile.time_zones",
        "ordinal": "3",
        "type": "vector<string>"
    },
    {
        "kind": "table",
        "name": "l/Profile"
    },
    {
        "kind": "library",
        "name": "l"
    }
]
`,
		},
		{
			name: "unions",
			fidl: `
library l;
type Left = struct {};
type Right = struct {};
type Either = strict union {
    1: left Left;
    2: right Right;
};
`,
			expected: `[
    {
        "kind": "union/member",
        "name": "l/Either.left",
        "ordinal": "1",
        "type": "l/Left"
    },
    {
        "kind": "union/member",
        "name": "l/Either.right",
        "ordinal": "2",
        "type": "l/Right"
    },
    {
        "kind": "union",
        "name": "l/Either",
        "strictness": "strict"
    },
    {
        "kind": "struct",
        "name": "l/Left"
    },
    {
        "kind": "struct",
        "name": "l/Right"
    },
    {
        "kind": "library",
        "name": "l"
    }
]
`,
		},
		{
			name: "protocols 1",
			fidl: `
library l;
protocol Calculator {
    Add(struct { a int32; b int32; }) -> (struct { sum int32; });
};
`,
			expected: `[
    {
        "kind": "protocol/member",
        "name": "l/Calculator.Add",
        "ordinal": "250442423443911233",
        "type": "(int32 a,int32 b) -> (int32 sum)"
    },
    {
        "kind": "protocol",
        "name": "l/Calculator"
    },
    {
        "kind": "library",
        "name": "l"
    }
]
`,
		},
		{
			name: "protocols 2",
			fidl: `
library l;
type Foo = struct {};
type Bar = struct {};
protocol P {
    M(struct { b box<Bar>; }) -> (struct { c Foo; });
};
`,
			expected: `[
    {
        "kind": "struct",
        "name": "l/Bar"
    },
    {
        "kind": "struct",
        "name": "l/Foo"
    },
    {
        "kind": "protocol/member",
        "name": "l/P.M",
        "ordinal": "1416054259560567967",
        "type": "(box<l/Bar> b) -> (l/Foo c)"
    },
    {
        "kind": "protocol",
        "name": "l/P"
    },
    {
        "kind": "library",
        "name": "l"
    }
]
`,
		},
		{
			name: "protocols 3",
			fidl: `
library l;
type Bar = struct {};
protocol P {};
protocol P2 {
    M1(resource struct { a client_end:P; });
    M2(resource struct { a client_end:<P, optional>; });
    M3(resource struct { a server_end:<P>; });
    M4(resource struct { a server_end:<P, optional>; });
};
`,
			expected: `[
    {
        "kind": "struct",
        "name": "l/Bar"
    },
    {
        "kind": "protocol",
        "name": "l/P"
    },
    {
        "kind": "protocol/member",
        "name": "l/P2.M1",
        "ordinal": "837411832102395320",
        "type": "(client_end:l/P a)"
    },
    {
        "kind": "protocol/member",
        "name": "l/P2.M2",
        "ordinal": "7643406716745546297",
        "type": "(client_end:<l/P,optional> a)"
    },
    {
        "kind": "protocol/member",
        "name": "l/P2.M3",
        "ordinal": "2712856865629095774",
        "type": "(server_end:l/P a)"
    },
    {
        "kind": "protocol/member",
        "name": "l/P2.M4",
        "ordinal": "8900715097515580538",
        "type": "(server_end:<l/P,optional> a)"
    },
    {
        "kind": "protocol",
        "name": "l/P2"
    },
    {
        "kind": "library",
        "name": "l"
    }
]
`,
		},
		{
			name: "protocols 4",
			fidl: `
library l;
protocol P {
    -> F1(struct { a int32; });
    F2() -> (struct { a int32; });
	F3() -> () error int32;
	F4();
};
`,
			expected: `[
    {
        "kind": "protocol/member",
        "name": "l/P.F1",
        "ordinal": "5135084091202286418",
        "type": " -> (int32 a)"
    },
    {
        "kind": "protocol/member",
        "name": "l/P.F2",
        "ordinal": "2448214607574469420",
        "type": "() -> (int32 a)"
    },
    {
        "kind": "protocol/member",
        "name": "l/P.F3",
        "ordinal": "542295173779636617",
        "type": "() -> (l/P_F3_Result result)"
    },
    {
        "kind": "protocol/member",
        "name": "l/P.F4",
        "ordinal": "7474367752247153959",
        "type": "()"
    },
    {
        "kind": "protocol",
        "name": "l/P"
    },
    {
        "kind": "struct",
        "name": "l/P_F3_Response"
    },
    {
        "kind": "union/member",
        "name": "l/P_F3_Result.err",
        "ordinal": "2",
        "type": "int32"
    },
    {
        "kind": "union/member",
        "name": "l/P_F3_Result.response",
        "ordinal": "1",
        "type": "l/P_F3_Response"
    },
    {
        "kind": "union",
        "name": "l/P_F3_Result",
        "strictness": "strict"
    },
    {
        "kind": "library",
        "name": "l"
    }
]
`,
		},
		{
			name: "protocols with named payloads",
			fidl: `
library l;
type Payload = struct {
    a bool;
};
protocol P {
    -> M1(Payload);
    M2() -> (Payload) error uint32;
};
`,
			expected: `[
    {
        "kind": "protocol/member",
        "name": "l/P.M1",
        "ordinal": "6412048159635322006",
        "type": " -> (bool a)"
    },
    {
        "kind": "protocol/member",
        "name": "l/P.M2",
        "ordinal": "4975997396601956357",
        "type": "() -> (l/P_M2_Result result)"
    },
    {
        "kind": "protocol",
        "name": "l/P"
    },
    {
        "kind": "union/member",
        "name": "l/P_M2_Result.err",
        "ordinal": "2",
        "type": "uint32"
    },
    {
        "kind": "union/member",
        "name": "l/P_M2_Result.response",
        "ordinal": "1",
        "type": "l/Payload"
    },
    {
        "kind": "union",
        "name": "l/P_M2_Result",
        "strictness": "strict"
    },
    {
        "kind": "struct/member",
        "name": "l/Payload.a",
        "ordinal": "1",
        "type": "bool"
    },
    {
        "kind": "struct",
        "name": "l/Payload"
    },
    {
        "kind": "library",
        "name": "l"
    }
]
`,
		},
		{
			name: "protocols with non-struct payloads",
			fidl: `
library l;
type U = flexible union {
    1: a uint64;
};
type T = table {
    1: b int32;
};
protocol P {
    M1(U)-> (T);
    M2(table {1: c bool; })-> (strict union {1: d uint32; });
};
`,
			expected: `[
    {
        "kind": "protocol/member",
        "name": "l/P.M1",
        "ordinal": "6412048159635322006",
        "type": "(l/U payload) -> (l/T payload)"
    },
    {
        "kind": "protocol/member",
        "name": "l/P.M2",
        "ordinal": "4975997396601956357",
        "type": "(l/PM2Request payload) -> (l/PM2Response payload)"
    },
    {
        "kind": "protocol",
        "name": "l/P"
    },
    {
        "kind": "table/member",
        "name": "l/PM2Request.c",
        "ordinal": "1",
        "type": "bool"
    },
    {
        "kind": "table",
        "name": "l/PM2Request"
    },
    {
        "kind": "union/member",
        "name": "l/PM2Response.d",
        "ordinal": "1",
        "type": "uint32"
    },
    {
        "kind": "union",
        "name": "l/PM2Response",
        "strictness": "strict"
    },
    {
        "kind": "table/member",
        "name": "l/T.b",
        "ordinal": "1",
        "type": "int32"
    },
    {
        "kind": "table",
        "name": "l/T"
    },
    {
        "kind": "union/member",
        "name": "l/U.a",
        "ordinal": "1",
        "type": "uint64"
    },
    {
        "kind": "union",
        "name": "l/U",
        "strictness": "flexible"
    },
    {
        "kind": "library",
        "name": "l"
    }
]
`,
		},
		{
			name: "protocols with errorable non-struct payloads",
			fidl: `
library l;
type T = table {
    1: b string;
};
protocol P {
    M1() -> (flexible union { 1: a bool; }) error uint32;
    M2() -> (T) error int32;
};
`,
			expected: `[
    {
        "kind": "protocol/member",
        "name": "l/P.M1",
        "ordinal": "6412048159635322006",
        "type": "() -> (l/P_M1_Result result)"
    },
    {
        "kind": "protocol/member",
        "name": "l/P.M2",
        "ordinal": "4975997396601956357",
        "type": "() -> (l/P_M2_Result result)"
    },
    {
        "kind": "protocol",
        "name": "l/P"
    },
    {
        "kind": "union/member",
        "name": "l/P_M1_Response.a",
        "ordinal": "1",
        "type": "bool"
    },
    {
        "kind": "union",
        "name": "l/P_M1_Response",
        "strictness": "flexible"
    },
    {
        "kind": "union/member",
        "name": "l/P_M1_Result.err",
        "ordinal": "2",
        "type": "uint32"
    },
    {
        "kind": "union/member",
        "name": "l/P_M1_Result.response",
        "ordinal": "1",
        "type": "l/P_M1_Response"
    },
    {
        "kind": "union",
        "name": "l/P_M1_Result",
        "strictness": "strict"
    },
    {
        "kind": "union/member",
        "name": "l/P_M2_Result.err",
        "ordinal": "2",
        "type": "int32"
    },
    {
        "kind": "union/member",
        "name": "l/P_M2_Result.response",
        "ordinal": "1",
        "type": "l/T"
    },
    {
        "kind": "union",
        "name": "l/P_M2_Result",
        "strictness": "strict"
    },
    {
        "kind": "table/member",
        "name": "l/T.b",
        "ordinal": "1",
        "type": "string"
    },
    {
        "kind": "table",
        "name": "l/T"
    },
    {
        "kind": "library",
        "name": "l"
    }
]
`,
		},
		{
			name: "check types",
			fidl: `
library l;
type S = struct {
    f1 string;
    f2 string:4;
    f3 string:<4, optional>;
};
`,
			expected: `[
    {
        "kind": "struct/member",
        "name": "l/S.f1",
        "ordinal": "1",
        "type": "string"
    },
    {
        "kind": "struct/member",
        "name": "l/S.f2",
        "ordinal": "2",
        "type": "string:4"
    },
    {
        "kind": "struct/member",
        "name": "l/S.f3",
        "ordinal": "3",
        "type": "string:<4,optional>"
    },
    {
        "kind": "struct",
        "name": "l/S"
    },
    {
        "kind": "library",
        "name": "l"
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
type A = struct {
  a l2.T;
};
`,
			expected: `[
    {
        "kind": "struct/member",
        "name": "l/A.a",
        "ordinal": "1",
        "type": "l2/T"
    },
    {
        "kind": "struct",
        "name": "l/A"
    },
    {
        "kind": "library",
        "name": "l"
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
type Foo = struct {};
type Bar = struct {};
protocol Calculator {
    compose l2.Inverter;
    Halve(l2.UnaryArg) -> (l2.UnaryArg);
    Add(struct { a l2.T; b Bar; }) -> (struct { c Foo; });
};
`,
			expected: `[
    {
        "kind": "struct",
        "name": "l/Bar"
    },
    {
        "kind": "protocol/member",
        "name": "l/Calculator.Add",
        "ordinal": "250442423443911233",
        "type": "(l2/T a,l/Bar b) -> (l/Foo c)"
    },
    {
        "kind": "protocol/member",
        "name": "l/Calculator.Halve",
        "ordinal": "7372493581703395840",
        "type": "(int32 num) -> (int32 num)"
    },
    {
        "kind": "protocol/member",
        "name": "l/Calculator.Invert",
        "ordinal": "2134776808183153853",
        "type": "(int32 num) -> (int32 num)"
    },
    {
        "kind": "protocol",
        "name": "l/Calculator"
    },
    {
        "kind": "struct",
        "name": "l/Foo"
    },
    {
        "kind": "library",
        "name": "l"
    }
]
`,
		},
		{
			name: "reserved keyword",
			fidl: `
library l;
type E = strict union {
    1: reserved;
    2: e int32;
};
type T = table {
    1: reserved;
    2: e int32;
};
`,
			expected: `[
    {
        "kind": "union/member",
        "name": "l/E.e",
        "ordinal": "2",
        "type": "int32"
    },
    {
        "kind": "union",
        "name": "l/E",
        "strictness": "strict"
    },
    {
        "kind": "table/member",
        "name": "l/T.e",
        "ordinal": "2",
        "type": "int32"
    },
    {
        "kind": "table",
        "name": "l/T"
    },
    {
        "kind": "library",
        "name": "l"
    }
]
`,
		},
	}
	runGenerateSummaryTests(t, tests)
}

func runGenerateSummaryTests(t *testing.T, tests []summaryTestCase) {
	t.Helper()
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			c := fidlgentest.EndToEndTest{T: t}
			if test.dep != "" {
				c = c.WithDependency(test.dep)
			}
			r := c.Single(test.fidl)
			s := Summarize(r)
			var b strings.Builder
			if err := s.WriteJSON(&b); err != nil {
				t.Fatalf("while writing JOSN: %v", err)
			}
			actual := strings.Split(b.String(), "\n")
			expected := strings.Split(test.expected, "\n")

			if diff := cmp.Diff(expected, actual); diff != "" {
				t.Errorf("got summary diff (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestLoadSummariesJSON(t *testing.T) {
	tests := []struct {
		name string
		json string
		want [][]ElementStr
	}{
		{
			name: "eof",
			json: "",
			want: [][]ElementStr{{}},
		}, {
			name: "empty list",
			json: "[]",
			want: [][]ElementStr{{}},
		}, {
			name: "library only",
			json: `[{"kind": "library", "name": "l"}]`,
			want: [][]ElementStr{
				{{Kind: libraryKind, Name: "l"}},
			},
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			summaries, err := LoadSummariesJSON(strings.NewReader(tt.json))
			if err != nil {
				t.Fatalf("LoadSummariesJSON(%q) got unexpected error: %v", tt.json, err)
			}

			if diff := cmp.Diff(tt.want, summaries); diff != "" {
				t.Fatalf("LoadSummariesJSON(%q) got diff (-want,+got):\n%s", tt.json, diff)
			}
		})
	}
}

func TestIsEmptyLibrary(t *testing.T) {
	tests := []struct {
		name     string
		fidl     string
		expected bool
	}{
		{
			name: "empty library",
			fidl: `
library l;
`,
			expected: true,
		},
		{
			name: "empty library with comments",
			fidl: `
// Regular comment

/// Doc comment
library l;
`,
			expected: true,
		},
		{
			name: "empty library with attribute",
			fidl: `
@some_attribute
library l;
`,
			expected: true,
		},
		{
			name: "nonempty library removed before HEAD",
			fidl: `
@available(platform="example", added=1, removed=2)
library l;

const FOO string = "foo";
`,
			expected: true,
		},
		{
			name: "nonempty library",
			fidl: `
library l;

const FOO string = "foo";
`,
			expected: false,
		},
	}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			c := fidlgentest.EndToEndTest{T: t}
			r := c.Single(test.fidl)
			s := Summarize(r)
			actual := s.IsEmptyLibrary()
			if actual != test.expected {
				t.Errorf("got %v, want %v for IsEmptyLibrary() on FIDL:\n%s\nWith summary:\n%+v", actual, test.expected, test.fidl, s)
			}
		})
	}
}
