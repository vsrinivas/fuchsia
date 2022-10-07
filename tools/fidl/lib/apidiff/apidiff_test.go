// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package apidiff

import (
	"encoding/json"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgentest"
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/summarize"
)

var (
	cmpOptions = cmpopts.IgnoreUnexported(Report{})
)

func TestApiDiff(t *testing.T) {
	t.Parallel()
	tests := []struct {
		name     string
		before   string
		after    string
		expected string
	}{
		// library
		{
			name: "library 1",
			before: `
library l;
`,
			after: `
library l;
`,
			expected: `
{}
`,
		},
		{
			name: "library 2",
			before: `
library l1;
`,
			after: `
library l2;
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l1",
            "before": {
                "kind": "library",
                "name": "l1"
            },
            "conclusion": "APIBreaking"
        },
        {
            "name": "l2",
            "after": {
                "kind": "library",
                "name": "l2"
            },
            "conclusion": "Compatible"
        }
    ]
}
`,
		},

		// const
		{
			name: "const",
			before: `
library l;
const FOO int32 = 32;
`,
			after: `
library l;
const FOO int32 = 32;
`,
			expected: `
{}
`,
		},
		{
			name: "const 2",
			before: `
library l;
const FOO int32 = 32;
`,
			after: `
library l;
const FOO string = "fuzzy";
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/FOO",
            "before": {
                "kind": "const",
                "name": "l/FOO",
                "type": "int32",
                "value": "32"
            },
            "after": {
                "kind": "const",
                "name": "l/FOO",
                "type": "string",
                "value": "fuzzy"
            },
            "conclusion": "APIBreaking"
        }
    ]
}
`,
		},
		{
			name: "const remove",
			before: `
library l;
const FOO int32 = 32;
`,
			after: `
library l;
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/FOO",
            "before": {
                "kind": "const",
                "name": "l/FOO",
                "type": "int32",
                "value": "32"
            },
            "conclusion": "APIBreaking"
        }
    ]
}
`,
		},
		{
			name: "const add",
			before: `
library l;
`,
			after: `
library l;
const FOO string = "fuzzy";
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/FOO",
            "after": {
                "kind": "const",
                "name": "l/FOO",
                "type": "string",
                "value": "fuzzy"
            },
            "conclusion": "Compatible"
        }
    ]
}
`,
		},
		{
			name: "const value change",
			before: `
library l;
const FOO int32 = 32;
`,
			after: `
library l;
const FOO int32 = 42;
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/FOO",
            "before": {
                "kind": "const",
                "name": "l/FOO",
                "type": "int32",
                "value": "32"
            },
            "after": {
                "kind": "const",
                "name": "l/FOO",
                "type": "int32",
                "value": "42"
            },
            "conclusion": "APIBreaking"
        }
    ]
}
`,
		},

		// bits
		{
			name: "bits member add to flexible",
			before: `
library l;
type Bits = flexible bits {
  BIT1 = 0x01;
};
`,
			after: `
library l;
type Bits = flexible bits {
  BIT1 = 0x01;
  BIT2 = 0x02;
};
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/Bits.BIT2",
            "after": {
                "kind": "bits/member",
                "name": "l/Bits.BIT2",
                "value": "2"
            },
            "conclusion": "Compatible"
        }
    ]
}
`,
		},
		{
			name: "bits member add to strict",
			before: `
library l;
type Bits = strict bits {
  BIT1 = 0x01;
};
`,
			after: `
library l;
type Bits = strict bits {
  BIT1 = 0x01;
  BIT2 = 0x02;
};
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/Bits.BIT2",
            "after": {
                "kind": "bits/member",
                "name": "l/Bits.BIT2",
                "value": "2"
            },
            "conclusion": "Compatible"
        }
    ]
}
`,
		},
		{
			name: "bits member remove",
			before: `
library l;
type Bits = bits {
  BIT1 = 0x01;
  BIT2 = 0x02;
};
`,
			after: `
library l;
type Bits = bits {
  BIT1 = 0x01;
};
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/Bits.BIT2",
            "before": {
                "kind": "bits/member",
                "name": "l/Bits.BIT2",
                "value": "2"
            },
            "conclusion": "APIBreaking"
        }
    ]
}
`,
		},
		{
			name: "bits add",
			before: `
library l;
`,
			after: `
library l;
type Bits = strict bits {
  BIT1 = 0x01;
};
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/Bits.BIT1",
            "after": {
                "kind": "bits/member",
                "name": "l/Bits.BIT1",
                "value": "1"
            },
            "conclusion": "Compatible"
        },
        {
            "name": "l/Bits",
            "after": {
                "kind": "bits",
                "name": "l/Bits",
                "strictness": "strict",
                "type": "uint32"
            },
            "conclusion": "Compatible"
        }
    ]
}
`,
		},
		{
			name: "bits remove",
			before: `
library l;
type Bits = strict bits {
  BIT1 = 0x01;
};
`,
			after: `
library l;
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/Bits.BIT1",
            "before": {
                "kind": "bits/member",
                "name": "l/Bits.BIT1",
                "value": "1"
            },
            "conclusion": "APIBreaking"
        },
        {
            "name": "l/Bits",
            "before": {
                "kind": "bits",
                "name": "l/Bits",
                "strictness": "strict",
                "type": "uint32"
            },
            "conclusion": "APIBreaking"
        }
    ]
}
`,
		},
		{
			name: "bits make flexible",
			before: `
library l;
type Bits = strict bits {
  BIT1 = 0x01;
};
`,
			after: `
library l;
type Bits = flexible bits {
  BIT1 = 0x01;
};
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/Bits",
            "before": {
                "kind": "bits",
                "name": "l/Bits",
                "strictness": "strict",
                "type": "uint32"
            },
            "after": {
                "kind": "bits",
                "name": "l/Bits",
                "strictness": "flexible",
                "type": "uint32"
            },
            "conclusion": "APIBreaking"
        }
    ]
}
`,
		},
		{
			name: "bits make strict",
			before: `
library l;
type Bits = flexible bits {
  BIT1 = 0x01;
};
`,
			after: `
library l;
type Bits = strict bits {
  BIT1 = 0x01;
};
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/Bits",
            "before": {
                "kind": "bits",
                "name": "l/Bits",
                "strictness": "flexible",
                "type": "uint32"
            },
            "after": {
                "kind": "bits",
                "name": "l/Bits",
                "strictness": "strict",
                "type": "uint32"
            },
            "conclusion": "APIBreaking"
        }
    ]
}
`,
		},
		{
			name: "bits change underlying type",
			before: `
library l;
type Bits = strict bits : uint32 {
  BIT1 = 0x01;
};
`,
			after: `
library l;
type Bits = strict bits : uint8 {
  BIT1 = 0x01;
};
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/Bits",
            "before": {
                "kind": "bits",
                "name": "l/Bits",
                "strictness": "strict",
                "type": "uint32"
            },
            "after": {
                "kind": "bits",
                "name": "l/Bits",
                "strictness": "strict",
                "type": "uint8"
            },
            "conclusion": "APIBreaking"
        }
    ]
}
`,
		},
		{
			name: "bits underlying type change",
			before: `
library l;
type Bits = strict bits : uint32 {
  BIT1 = 0x01;
};
`,
			after: `
library l;
type Bits = strict bits : uint8 {
  BIT1 = 0x01;
};
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/Bits",
            "before": {
                "kind": "bits",
                "name": "l/Bits",
                "strictness": "strict",
                "type": "uint32"
            },
            "after": {
                "kind": "bits",
                "name": "l/Bits",
                "strictness": "strict",
                "type": "uint8"
            },
            "conclusion": "APIBreaking"
        }
    ]
}
`,
		},
		{
			name: "bits change type and strictness to flexible",
			before: `
library l;
type Bits = strict bits : uint32 {
  BIT1 = 0x01;
};
`,
			after: `
library l;
type Bits = flexible bits : uint8 {
  BIT1 = 0x01;
};
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/Bits",
            "before": {
                "kind": "bits",
                "name": "l/Bits",
                "strictness": "strict",
                "type": "uint32"
            },
            "after": {
                "kind": "bits",
                "name": "l/Bits",
                "strictness": "flexible",
                "type": "uint8"
            },
            "conclusion": "APIBreaking"
        }
    ]
}
`,
		},
		{
			name: "bits value change",
			before: `
library l;
type Bits = flexible bits {
  BIT1 = 0x01;
};
`,
			after: `
library l;
type Bits = flexible bits {
  BIT1 = 0x02;
};
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/Bits.BIT1",
            "before": {
                "kind": "bits/member",
                "name": "l/Bits.BIT1",
                "value": "1"
            },
            "after": {
                "kind": "bits/member",
                "name": "l/Bits.BIT1",
                "value": "2"
            },
            "conclusion": "APIBreaking"
        }
    ]
}
`,
		},

		// enum
		{
			name: "enum add",
			before: `
library l;
`,
			after: `
library l;
type Enum = strict enum {
  WATER = 1;
};
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/Enum.WATER",
            "after": {
                "kind": "enum/member",
                "name": "l/Enum.WATER",
                "value": "1"
            },
            "conclusion": "Compatible"
        },
        {
            "name": "l/Enum",
            "after": {
                "kind": "enum",
                "name": "l/Enum",
                "strictness": "strict",
                "type": "uint32"
            },
            "conclusion": "Compatible"
        }
    ]
}
`,
		},
		{
			name: "enum value add to flexible",
			before: `
library l;
type Enum = flexible enum {
  WATER = 1;
};
`,
			after: `
library l;
type Enum = flexible enum {
  WATER = 1;
  FIRE = 2;
};
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/Enum.FIRE",
            "after": {
                "kind": "enum/member",
                "name": "l/Enum.FIRE",
                "value": "2"
            },
            "conclusion": "Compatible"
        }
    ]
}
`,
		},
		{
			name: "enum value add to strict",
			before: `
library l;
type Enum = strict enum {
  WATER = 1;
};
`,
			after: `
library l;
type Enum = strict enum {
  WATER = 1;
  FIRE = 2;
};
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/Enum.FIRE",
            "after": {
                "kind": "enum/member",
                "name": "l/Enum.FIRE",
                "value": "2"
            },
            "conclusion": "APIBreaking"
        }
    ]
}
`,
		},
		{
			name: "enum value remove",
			before: `
library l;
type Enum = enum {
  WATER = 1;
  FIRE = 2;
};
`,
			after: `
library l;
type Enum = enum {
  WATER = 1;
};
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/Enum.FIRE",
            "before": {
                "kind": "enum/member",
                "name": "l/Enum.FIRE",
                "value": "2"
            },
            "conclusion": "APIBreaking"
        }
    ]
}
`,
		},
		{
			name: "enum strictness change to flexible",
			before: `
library l;
type Enum = strict enum {
  WATER = 1;
};
`,
			after: `
library l;
type Enum = flexible enum {
  WATER = 1;
};
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/Enum",
            "before": {
                "kind": "enum",
                "name": "l/Enum",
                "strictness": "strict",
                "type": "uint32"
            },
            "after": {
                "kind": "enum",
                "name": "l/Enum",
                "strictness": "flexible",
                "type": "uint32"
            },
            "conclusion": "APIBreaking"
        }
    ]
}
`,
		},
		{
			name: "enum strictness change to strict",
			before: `
library l;
type Enum = flexible enum {
  WATER = 1;
};
`,
			after: `
library l;
type Enum = strict enum {
  WATER = 1;
};
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/Enum",
            "before": {
                "kind": "enum",
                "name": "l/Enum",
                "strictness": "flexible",
                "type": "uint32"
            },
            "after": {
                "kind": "enum",
                "name": "l/Enum",
                "strictness": "strict",
                "type": "uint32"
            },
            "conclusion": "APIBreaking"
        }
    ]
}
`,
		},
		{
			name: "enum underlying type change",
			before: `
library l;
type Enum = strict enum : uint32 {
  WATER = 1;
};
`,
			after: `
library l;
type Enum = strict enum : uint8 {
  WATER = 1;
};
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/Enum",
            "before": {
                "kind": "enum",
                "name": "l/Enum",
                "strictness": "strict",
                "type": "uint32"
            },
            "after": {
                "kind": "enum",
                "name": "l/Enum",
                "strictness": "strict",
                "type": "uint8"
            },
            "conclusion": "APIBreaking"
        }
    ]
}
`,
		},
		{
			name: "enum value change",
			before: `
library l;
type Enum = enum {
  WATER = 1;
};
`,
			after: `
library l;
type Enum = enum {
  WATER = 2;
};
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/Enum.WATER",
            "before": {
                "kind": "enum/member",
                "name": "l/Enum.WATER",
                "value": "1"
            },
            "after": {
                "kind": "enum/member",
                "name": "l/Enum.WATER",
                "value": "2"
            },
            "conclusion": "APIBreaking"
        }
    ]
}
`,
		},

		// struct
		{
			name: "struct add",
			before: `
library l;
`,
			after: `
library l;
type Struct = struct {};
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/Struct",
            "after": {
                "kind": "struct",
                "name": "l/Struct"
            },
            "conclusion": "Compatible"
        }
    ]
}
`,
		},
		{
			name: "struct remove",
			before: `
library l;
type Struct = struct {};
`,
			after: `
library l;
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/Struct",
            "before": {
                "kind": "struct",
                "name": "l/Struct"
            },
            "conclusion": "APIBreaking"
        }
    ]
}
`,
		},
		{
			name: "struct become resource",
			before: `
library l;
type Struct = struct {};
`,
			after: `
library l;
type Struct = resource struct {};
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/Struct",
            "before": {
                "kind": "struct",
                "name": "l/Struct"
            },
            "after": {
                "kind": "struct",
                "name": "l/Struct",
                "resourceness": "resource"
            },
            "conclusion": "APIBreaking"
        }
    ]
}
`,
		},
		{
			name: "struct unbecome resource",
			before: `
library l;
type Struct = resource struct {};
`,
			after: `
library l;
type Struct = struct {};
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/Struct",
            "before": {
                "kind": "struct",
                "name": "l/Struct",
                "resourceness": "resource"
            },
            "after": {
                "kind": "struct",
                "name": "l/Struct"
            },
            "conclusion": "APIBreaking"
        }
    ]
}
`,
		},
		{
			// The addition of the member should not be considered ABI breaking
			// because it is added at the same time as the struct.
			name: "struct with member add",
			after: `
library l;
type Struct = struct {
    member int32;
};
`,
			before: `
library l;
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/Struct.member",
            "after": {
                "kind": "struct/member",
                "name": "l/Struct.member",
                "ordinal": "1",
                "type": "int32"
            },
            "conclusion": "Compatible"
        },
        {
            "name": "l/Struct",
            "after": {
                "kind": "struct",
                "name": "l/Struct"
            },
            "conclusion": "Compatible"
        }
    ]
}
`,
		},
		{
			name: "struct/member add",
			before: `
library l;
type Struct = struct {};
`,
			after: `
library l;
type Struct = struct {
  foo int32;
};
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/Struct.foo",
            "after": {
                "kind": "struct/member",
                "name": "l/Struct.foo",
                "ordinal": "1",
                "type": "int32"
            },
            "conclusion": "APIBreaking"
        }
    ]
}
`,
		},
		{
			name: "struct remove member",
			before: `
library l;
type Struct = struct {
	foo int32;
};
`,
			after: `
library l;
type Struct = struct {
};
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/Struct.foo",
            "before": {
                "kind": "struct/member",
                "name": "l/Struct.foo",
                "ordinal": "1",
                "type": "int32"
            },
            "conclusion": "APIBreaking"
        }
    ]
}
`,
		},
		{
			name: "struct change type",
			before: `
library l;
type Struct = struct {
	foo int32;
};
`,
			after: `
library l;
type Struct = struct {
	foo string;
};
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/Struct.foo",
            "before": {
                "kind": "struct/member",
                "name": "l/Struct.foo",
                "ordinal": "1",
                "type": "int32"
            },
            "after": {
                "kind": "struct/member",
                "name": "l/Struct.foo",
                "ordinal": "1",
                "type": "string"
            },
            "conclusion": "APIBreaking"
        }
    ]
}
`,
		},
		{
			name: "struct default value change",
			before: `
library l;
type Struct = struct {
	@allow_deprecated_struct_defaults
	foo int32 = 1;
};
`,
			after: `
library l;
type Struct = struct {
	@allow_deprecated_struct_defaults
	foo int32 = 2;
};
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/Struct.foo",
            "before": {
                "kind": "struct/member",
                "name": "l/Struct.foo",
                "ordinal": "1",
                "type": "int32",
                "value": "1"
            },
            "after": {
                "kind": "struct/member",
                "name": "l/Struct.foo",
                "ordinal": "1",
                "type": "int32",
                "value": "2"
            },
            "conclusion": "APIBreaking"
        }
    ]
}
`,
		},
		{
			name: "struct reorder member",
			before: `
library l;
type Struct = struct {
	foo int32;
    bar string;
};
`,
			after: `
library l;
type Struct = struct {
    bar string;
	foo int32;
};
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/Struct.bar",
            "before": {
                "kind": "struct/member",
                "name": "l/Struct.bar",
                "ordinal": "2",
                "type": "string",
                "value": ""
            },
            "after": {
                "kind": "struct/member",
                "name": "l/Struct.bar",
                "ordinal": "1",
                "type": "string",
                "value": ""
            },
            "conclusion": "APIBreaking"
        },
        {
            "name": "l/Struct.foo",
            "before": {
                "kind": "struct/member",
                "name": "l/Struct.foo",
                "ordinal": "1",
                "type": "int32",
                "value": ""
            },
            "after": {
                "kind": "struct/member",
                "name": "l/Struct.foo",
                "ordinal": "2",
                "type": "int32",
                "value": ""
            },
            "conclusion": "APIBreaking"
        }
    ]
}
`,
		},

		// table
		{
			name: "table add",
			before: `
library l;
`,
			after: `
library l;
type T = table {};
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/T",
            "after": {
                "kind": "table",
                "name": "l/T"
            },
            "conclusion": "Compatible"
        }
    ]
}
`,
		},
		{
			name: "table remove",
			before: `
library l;
type T = table {};
`,
			after: `
library l;
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/T",
            "before": {
                "kind": "table",
                "name": "l/T"
            },
            "conclusion": "APIBreaking"
        }
    ]
}
`,
		},
		{
			name: "table become resource",
			before: `
library l;
type T = table {};
`,
			after: `
library l;
type T = resource table {};
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/T",
            "before": {
                "kind": "table",
                "name": "l/T"
            },
            "after": {
                "kind": "table",
                "name": "l/T",
                "resourceness": "resource"
            },
            "conclusion": "APIBreaking"
        }
    ]
}
`,
		},
		{
			name: "table unbecome resource",
			before: `
library l;
type T = resource table {};
`,
			after: `
library l;
type T = table {};
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/T",
            "before": {
                "kind": "table",
                "name": "l/T",
                "resourceness": "resource"
            },
            "after": {
                "kind": "table",
                "name": "l/T"
            },
            "conclusion": "APIBreaking"
        }
    ]
}
`,
		},
		{
			name: "table add member",
			before: `
library l;
type T = table {};
`,
			after: `
library l;
type T = table {
  1: foo int32;
};
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/T.foo",
            "after": {
                "kind": "table/member",
                "name": "l/T.foo",
                "ordinal": "1",
                "type": "int32"
            },
            "conclusion": "Compatible"
        }
    ]
}
`,
		},
		{
			name: "table remove member",
			before: `
library l;
type T = table {
  1: foo int32;
};
`,
			after: `
library l;
type T = table {
};
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/T.foo",
            "before": {
                "kind": "table/member",
                "name": "l/T.foo",
                "ordinal": "1",
                "type": "int32"
            },
            "conclusion": "APIBreaking"
        }
    ]
}
`,
		},
		{
			name: "table make member reserved",
			before: `
library l;
type T = table {
  1: foo int32;
  2: bar int32;
};
`,
			after: `
library l;
type T = table {
  1: reserved;
  2: bar int32;
};
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/T.foo",
            "before": {
                "kind": "table/member",
                "name": "l/T.foo",
                "ordinal": "1",
                "type": "int32"
            },
            "conclusion": "APIBreaking"
        }
    ]
}
`,
		},
		{
			name: "table change type",
			before: `
library l;
type T = table {
  1: foo int32;
};
`,
			after: `
library l;
type T = table {
  1: foo string;
};
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/T.foo",
            "before": {
                "kind": "table/member",
                "name": "l/T.foo",
                "ordinal": "1",
                "type": "int32"
            },
            "after": {
                "kind": "table/member",
                "name": "l/T.foo",
                "ordinal": "1",
                "type": "string"
            },
            "conclusion": "APIBreaking"
        }
    ]
}
`,
		},
		{
			name: "table change ordinal",
			before: `
library l;
type T = table {
    1: foo int32;
};
`,
			after: `
library l;
type T = table {
    1: reserved;
    2: foo int32;
};
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/T.foo",
            "before": {
                "kind": "table/member",
                "name": "l/T.foo",
                "ordinal": "1",
                "type": "int32"
            },
            "after": {
                "kind": "table/member",
                "name": "l/T.foo",
                "ordinal": "2",
                "type": "int32"
            },
            "conclusion": "APICompatibleButABIBreaking"
        }
    ]
}
`,
		},

		// union
		{
			name: "union add",
			before: `
library l;
`,
			after: `
library l;
type T = strict union {
  1: foo int32;
};
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/T.foo",
            "after": {
                "kind": "union/member",
                "name": "l/T.foo",
                "ordinal": "1",
                "type": "int32"
            },
            "conclusion": "Compatible"
        },
        {
            "name": "l/T",
            "after": {
                "kind": "union",
                "name": "l/T",
                "strictness": "strict"
            },
            "conclusion": "Compatible"
        }
    ]
}
`,
		},
		{
			name: "union remove",
			before: `
library l;
type T = strict union {
  1: foo int32;
};
`,
			after: `
library l;
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/T.foo",
            "before": {
                "kind": "union/member",
                "name": "l/T.foo",
                "ordinal": "1",
                "type": "int32"
            },
            "conclusion": "APIBreaking"
        },
        {
            "name": "l/T",
            "before": {
                "kind": "union",
                "name": "l/T",
                "strictness": "strict"
            },
            "conclusion": "APIBreaking"
        }
    ]
}
`,
		},
		{
			name: "union make reserved",
			before: `
library l;
type T = union {
  1: foo int32;
  2: bar int32;
};
`,
			after: `
library l;
type T = union {
  1: reserved;
  2: bar int32;
};
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/T.foo",
            "before": {
                "kind": "union/member",
                "name": "l/T.foo",
                "ordinal": "1",
                "type": "int32"
            },
            "conclusion": "APIBreaking"
        }
    ]
}
`,
		},
		{
			name: "union add reserved",
			before: `
library l;
type T = union {
  1: reserved;
  2: foo int32;
};
`,
			after: `
library l;
type T = union {
  1: reserved;
  2: foo int32;
  3: reserved;
};
`,
			expected: `
{}
`,
		},
		{
			name: "union remove with reserved",
			before: `
library l;
type T = strict union {
  1: reserved;
  2: foo int32;
};
`,
			after: `
library l;
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/T.foo",
            "before": {
                "kind": "union/member",
                "name": "l/T.foo",
                "ordinal": "2",
                "type": "int32"
            },
            "conclusion": "APIBreaking"
        },
        {
            "name": "l/T",
            "before": {
                "kind": "union",
                "name": "l/T",
                "strictness": "strict"
            },
            "conclusion": "APIBreaking"
        }
    ]
}
`,
		},
		{
			name: "union become resource",
			before: `
library l;
type T = strict union {
  1: foo int32;
};
`,
			after: `
library l;
type T = strict resource union {
  1: foo int32;
};
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/T",
            "before": {
                "kind": "union",
                "name": "l/T",
                "strictness": "strict"
            },
            "after": {
                "kind": "union",
                "name": "l/T",
                "resourceness": "resource",
                "strictness": "strict"
            },
            "conclusion": "APIBreaking"
        }
    ]
}
`,
		},
		{
			name: "union unbecome resource",
			before: `
library l;
type T = strict resource union {
  1: bar int32;
};
`,
			after: `
library l;
type T = strict union {
  1: bar int32;
};
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/T",
            "before": {
                "kind": "union",
                "name": "l/T",
                "resourceness": "resource",
                "strictness": "strict"
            },
            "after": {
                "kind": "union",
                "name": "l/T",
                "strictness": "strict"
            },
            "conclusion": "APIBreaking"
        }
    ]
}
`,
		},
		{
			name: "flexible union add member",
			before: `
library l;
type T = flexible union {
  1: bar int32;
};
`,
			after: `
library l;
type T = flexible union {
  1: bar int32;
  2: foo int32;
};
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/T.foo",
            "after": {
                "kind": "union/member",
                "name": "l/T.foo",
                "ordinal": "2",
                "type": "int32"
            },
            "conclusion": "Compatible"
        }
    ]
}
`,
		},
		{
			name: "strict union add member",
			before: `
library l;
type T = strict union {
  1: bar int32;
};
`,
			after: `
library l;
type T = strict union {
  1: bar int32;
  2: foo int32;
};
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/T.foo",
            "after": {
                "kind": "union/member",
                "name": "l/T.foo",
                "ordinal": "2",
                "type": "int32"
            },
            "conclusion": "APIBreaking"
        }
    ]
}
`,
		},
		{
			name: "strict union remove member",
			before: `
library l;
type T = strict union {
  1: bar int32;
  2: foo int32;
};
`,
			after: `
library l;
type T = strict union {
  1: bar int32;
};
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/T.foo",
            "before": {
                "kind": "union/member",
                "name": "l/T.foo",
                "ordinal": "2",
                "type": "int32"
            },
            "conclusion": "APIBreaking"
        }
    ]
}
`,
		},
		{
			name: "union change type",
			before: `
library l;
type T = union {
  1: foo int32;
};
`,
			after: `
library l;
type T = union {
  1: foo string;
};
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/T.foo",
            "before": {
                "kind": "union/member",
                "name": "l/T.foo",
                "ordinal": "1",
                "type": "int32"
            },
            "after": {
                "kind": "union/member",
                "name": "l/T.foo",
                "ordinal": "1",
                "type": "string"
            },
            "conclusion": "APIBreaking"
        }
    ]
}
`,
		},
		{
			name: "union change ordinal",
			before: `
library l;
type T = union {
    1: foo int32;
};
`,
			after: `
library l;
type T = union {
    1: reserved;
    2: foo int32;
};
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/T.foo",
            "before": {
                "kind": "union/member",
                "name": "l/T.foo",
                "ordinal": "1",
                "type": "int32"
            },
            "after": {
                "kind": "union/member",
                "name": "l/T.foo",
                "ordinal": "2",
                "type": "int32"
            },
            "conclusion": "APICompatibleButABIBreaking"
        }
    ]
}
`,
		},

		// protocol
		{
			name: "protocol add",
			before: `
library l;
`,
			after: `
library l;
protocol T {};
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/T",
            "after": {
                "kind": "protocol",
                "name": "l/T"
            },
            "conclusion": "Compatible"
        }
    ]
}
`,
		},
		{
			name: "protocol remove",
			before: `
library l;
protocol T {};
`,
			after: `
library l;
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/T",
            "before": {
                "kind": "protocol",
                "name": "l/T"
            },
            "conclusion": "APIBreaking"
        }
    ]
}
`,
		},
		{
			name: "protocol member add",
			before: `
library l;
protocol T {
};
`,
			after: `
library l;
protocol T {
  Test(struct { t int32; }) -> ();
};
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/T.Test",
            "after": {
                "kind": "protocol/member",
                "name": "l/T.Test",
                "ordinal": "7985249320572540149",
                "type": "(int32 t) -> ()"
            },
            "conclusion": "Compatible"
        }
    ]
}
`,
		},
		{
			name: "protocol member remove",
			before: `
library l;
protocol T {
  Test(struct { t int32; }) -> ();
};
`,
			after: `
library l;
protocol T {
};
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/T.Test",
            "before": {
                "kind": "protocol/member",
                "name": "l/T.Test",
                "ordinal": "7985249320572540149",
                "type": "(int32 t) -> ()"
            },
            "conclusion": "APIBreaking"
        }
    ]
}
`,
		},
		{
			name: "protocol member type change",
			before: `
library l;
protocol T {
  Test(struct { t int32; }) -> ();
};
`,
			after: `
library l;
protocol T {
  Test(struct { t int32; u int32; }) -> ();
};
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/T.Test",
            "before": {
                "kind": "protocol/member",
                "name": "l/T.Test",
                "ordinal": "7985249320572540149",
                "type": "(int32 t) -> ()"
            },
            "after": {
                "kind": "protocol/member",
                "name": "l/T.Test",
                "ordinal": "7985249320572540149",
                "type": "(int32 t,int32 u) -> ()"
            },
            "conclusion": "APIBreaking"
        }
    ]
}
`,
		},
		{
			name: "protocol member ordinal change",
			before: `
library l;
protocol T {
    Test(struct { t int32; }) -> ();
};
`,
			after: `
library l;
protocol T {
    @selector("notl/NotT.NotTest")
    Test(struct { t int32; }) -> ();
};
`,
			expected: `
{
    "api_diff": [
        {
            "name": "l/T.Test",
            "before": {
                "kind": "protocol/member",
                "name": "l/T.Test",
                "ordinal": "7985249320572540149",
                "type": "(int32 t) -> ()"
            },
            "after": {
                "kind": "protocol/member",
                "name": "l/T.Test",
                "ordinal": "8693951483982195746",
                "type": "(int32 t) -> ()"
            },
            "conclusion": "APICompatibleButABIBreaking"
        }
    ]
}
`,
		},
		// TODO(fxbug.dev/7807): Add aliases and newtypes to summaries and diffs
		// once they are fully implemented.
	}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			c := fidlgentest.EndToEndTest{T: t}
			brd := strings.NewReader(
				summarizeOne(t, c.Single(test.before)))
			ard := strings.NewReader(
				summarizeOne(t, c.Single(test.after)))
			summaries, err := summarize.LoadSummariesJSON(brd, ard)
			if err != nil {
				t.Fatalf("while loading summaries: %v", err)
			}
			actual, err := Compute(summaries[0], summaries[1])
			if err != nil {
				t.Fatalf("while computing diff: %v", err)
			}
			var expected Report
			if err := json.Unmarshal([]byte(test.expected), &expected); err != nil {
				t.Fatalf("unexpected error while unmarshaling expected data: %v", err)
			}
			if diff := cmp.Diff(expected, actual, cmpOptions); diff != "" {
				t.Errorf("want:\n\t%+v\n\tgot:\n\t%+v\n\tdiff:\n\t%v", expected, actual, diff)
			}
		})
	}
}

func summarizeOne(t *testing.T, r fidlgen.Root) string {
	t.Helper()
	s := summarize.Summarize(r)
	var buf strings.Builder
	if err := s.WriteJSON(&buf); err != nil {
		t.Fatalf("error while summarizing: %v", err)
	}
	return buf.String()
}
