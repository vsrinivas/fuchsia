// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package apidiff

import (
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
api_diff:
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
api_diff:
- name: l1
  before: "library l1"
  conclusion: APIBreaking
- name: l2
  after: "library l2"
  conclusion: SourceCompatible
`,
		},

		// const
		{
			name: "const",
			before: `
library l;
const int32 FOO = 32;
`,
			after: `
library l;
const int32 FOO = 32;
`,
			expected: `
api_diff:
`,
		},
		{
			name: "const 2",
			before: `
library l;
const int32 FOO = 32;
`,
			after: `
library l;
const string FOO = "fuzzy";
`,
			expected: `
api_diff:
- name: l/FOO
  before: "const l/FOO int32"
  after: "const l/FOO string"
  conclusion: APIBreaking
`,
		},
		{
			name: "const remove",
			before: `
library l;
const int32 FOO = 32;
`,
			after: `
library l;
`,
			expected: `
api_diff:
- name: l/FOO
  before: "const l/FOO int32"
  conclusion: APIBreaking
`,
		},
		{
			name: "const add",
			before: `
library l;
`,
			after: `
library l;
const string FOO = "fuzzy";
`,
			expected: `
api_diff:
- name: l/FOO
  after: "const l/FOO string"
  conclusion: SourceCompatible
`,
		},

		// bits
		{
			name: "bits member add to flexible",
			before: `
library l;
flexible bits Bits {
  BIT1 = 0x01;
};
`,
			after: `
library l;
flexible bits Bits {
  BIT1 = 0x01;
  BIT2 = 0x02;
};
`,
			expected: `
api_diff:
- name: l/Bits.BIT2
  after: "bits/member l/Bits.BIT2"
  conclusion: SourceCompatible
`,
		},
		{
			name: "bits member add to strict",
			before: `
library l;
strict bits Bits {
  BIT1 = 0x01;
};
`,
			after: `
library l;
bits Bits {
  BIT1 = 0x01;
  BIT2 = 0x02;
};
`,
			expected: `
api_diff:
- name: l/Bits.BIT2
  after: "bits/member l/Bits.BIT2"
  conclusion: SourceCompatible
`,
		},
		{
			name: "bits member remove",
			before: `
library l;
bits Bits {
  BIT1 = 0x01;
  BIT2 = 0x02;
};
`,
			after: `
library l;
bits Bits {
  BIT1 = 0x01;
};
`,
			expected: `
api_diff:
- name: l/Bits.BIT2
  before: "bits/member l/Bits.BIT2"
  conclusion: APIBreaking
`,
		},
		{
			name: "bits add",
			before: `
library l;
`,
			after: `
library l;
bits Bits {
  BIT1 = 0x01;
};
`,
			expected: `
api_diff:
- name: l/Bits.BIT1
  after: "bits/member l/Bits.BIT1"
  conclusion: SourceCompatible
- name: l/Bits
  after: "strict bits l/Bits uint32"
  conclusion: SourceCompatible
`,
		},
		{
			name: "bits remove",
			before: `
library l;
bits Bits {
  BIT1 = 0x01;
};
`,
			after: `
library l;
`,
			expected: `
api_diff:
- name: l/Bits.BIT1
  before: "bits/member l/Bits.BIT1"
  conclusion: APIBreaking
- name: l/Bits
  before: "strict bits l/Bits uint32"
  conclusion: APIBreaking
`,
		},
		{
			name: "bits make flexible",
			before: `
library l;
bits Bits {
  BIT1 = 0x01;
};
`,
			after: `
library l;
flexible bits Bits {
  BIT1 = 0x01;
};
`,
			expected: `
api_diff:
- name: l/Bits
  before: "strict bits l/Bits uint32"
  after: "flexible bits l/Bits uint32"
  conclusion: Transitionable
`,
		},
		{
			name: "bits make strict",
			before: `
library l;
flexible bits Bits {
  BIT1 = 0x01;
};
`,
			after: `
library l;
strict bits Bits {
  BIT1 = 0x01;
};
`,
			expected: `
api_diff:
- name: l/Bits
  before: "flexible bits l/Bits uint32"
  after: "strict bits l/Bits uint32"
  conclusion: Transitionable
`,
		},
		{
			name: "bits change underlying type",
			before: `
library l;
bits Bits : uint32 {
  BIT1 = 0x01;
};
`,
			after: `
library l;
bits Bits : uint8 {
  BIT1 = 0x01;
};
`,
			expected: `
api_diff:
- name: l/Bits
  before: "strict bits l/Bits uint32"
  after: "strict bits l/Bits uint8"
  conclusion: APIBreaking
`,
		},
		{
			name: "bits underlying type change",
			before: `
library l;
strict bits Bits : uint32 {
  BIT1 = 0x01;
};
`,
			after: `
library l;
strict bits Bits : uint8 {
  BIT1 = 0x01;
};
`,
			expected: `
api_diff:
- name: l/Bits
  before: "strict bits l/Bits uint32"
  after: "strict bits l/Bits uint8"
  conclusion: APIBreaking
`,
		},
		{
			name: "bits change type and strictness to flexible",
			before: `
library l;
strict bits Bits : uint32 {
  BIT1 = 0x01;
};
`,
			after: `
library l;
flexible bits Bits : uint8 {
  BIT1 = 0x01;
};
`,
			expected: `
api_diff:
- name: l/Bits
  before: "strict bits l/Bits uint32"
  after: "flexible bits l/Bits uint8"
  conclusion: APIBreaking
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
enum Enum {
  WATER = 1;
};
`,
			expected: `
api_diff:
- name: l/Enum.WATER
  after: "enum/member l/Enum.WATER"
  conclusion: SourceCompatible
- name: l/Enum
  after: "strict enum l/Enum uint32"
  conclusion: SourceCompatible
`,
		},
		{
			name: "enum value add to flexible",
			before: `
library l;
flexible enum Enum {
  WATER = 1;
};
`,
			after: `
library l;
flexible enum Enum {
  WATER = 1;
  FIRE = 2;
};
`,
			expected: `
api_diff:
- name: l/Enum.FIRE
  after: "enum/member l/Enum.FIRE"
  conclusion: SourceCompatible
`,
		},
		{
			name: "enum value add to strict",
			before: `
library l;
strict enum Enum {
  WATER = 1;
};
`,
			after: `
library l;
strict enum Enum {
  WATER = 1;
  FIRE = 2;
};
`,
			expected: `
api_diff:
- name: l/Enum.FIRE
  after: "enum/member l/Enum.FIRE"
  conclusion: APIBreaking
`,
		},
		{
			name: "enum value remove",
			before: `
library l;
enum Enum {
  WATER = 1;
  FIRE = 2;
};
`,
			after: `
library l;
enum Enum {
  WATER = 1;
};
`,
			expected: `
api_diff:
- name: l/Enum.FIRE
  before: "enum/member l/Enum.FIRE"
  conclusion: APIBreaking
`,
		},
		{
			name: "enum strictness change to flexible",
			before: `
library l;
strict enum Enum {
  WATER = 1;
};
`,
			after: `
library l;
flexible enum Enum {
  WATER = 1;
};
`,
			expected: `
api_diff:
- name: l/Enum
  before: "strict enum l/Enum uint32"
  after: "flexible enum l/Enum uint32"
  conclusion: Transitionable
`,
		},
		{
			name: "enum strictness change to strict",
			before: `
library l;
flexible enum Enum {
  WATER = 1;
};
`,
			after: `
library l;
strict enum Enum {
  WATER = 1;
};
`,
			expected: `
api_diff:
- name: l/Enum
  before: "flexible enum l/Enum uint32"
  after: "strict enum l/Enum uint32"
  conclusion: Transitionable
`,
		},
		{
			name: "enum underlying type change",
			before: `
library l;
enum Enum : uint32 {
  WATER = 1;
};
`,
			after: `
library l;
enum Enum : uint8 {
  WATER = 1;
};
`,
			expected: `
api_diff:
- name: l/Enum
  before: "strict enum l/Enum uint32"
  after: "strict enum l/Enum uint8"
  conclusion: APIBreaking
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
struct Struct {};
`,
			expected: `
api_diff:
- name: l/Struct
  after: "struct l/Struct"
  conclusion: SourceCompatible
`,
		},
		{
			name: "struct remove",
			before: `
library l;
struct Struct {};
`,
			after: `
library l;
`,
			expected: `
api_diff:
- name: l/Struct
  before: "struct l/Struct"
  conclusion: APIBreaking
`,
		},
		{
			name: "struct become resource",
			before: `
library l;
struct Struct {};
`,
			after: `
library l;
resource struct Struct {};
`,
			expected: `
api_diff:
- name: l/Struct
  before: "struct l/Struct"
  after: "resource struct l/Struct"
  conclusion: APIBreaking
`,
		},
		{
			name: "struct unbecome resource",
			before: `
library l;
resource struct Struct {};
`,
			after: `
library l;
struct Struct {};
`,
			expected: `
api_diff:
- name: l/Struct
  before: "resource struct l/Struct"
  after: "struct l/Struct"
  conclusion: APIBreaking
`,
		},
		{
			name: "struct add member",
			before: `
library l;
struct Struct {};
`,
			after: `
library l;
struct Struct {
  int32 foo;
};
`,
			expected: `
api_diff:
- name: l/Struct.foo
  after: "struct/member l/Struct.foo int32"
  conclusion: APIBreaking
`,
		},
		{
			name: "struct remove member",
			before: `
library l;
struct Struct {
  int32 foo;
};
`,
			after: `
library l;
struct Struct {
};
`,
			expected: `
api_diff:
- name: l/Struct.foo
  before: "struct/member l/Struct.foo int32"
  conclusion: APIBreaking
`,
		},
		{
			name: "struct change type",
			before: `
library l;
struct Struct {
  int32 foo;
};
`,
			after: `
library l;
struct Struct {
  string foo;
};
`,
			expected: `
api_diff:
- name: l/Struct.foo
  before: "struct/member l/Struct.foo int32"
  after: "struct/member l/Struct.foo string"
  conclusion: APIBreaking
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
table T {};
`,
			expected: `
api_diff:
- name: l/T
  after: "table l/T"
  conclusion: SourceCompatible
`,
		},
		{
			name: "table remove",
			before: `
library l;
table T {};
`,
			after: `
library l;
`,
			expected: `
api_diff:
- name: l/T
  before: "table l/T"
  conclusion: APIBreaking
`,
		},
		{
			name: "table become resource",
			before: `
library l;
table T {};
`,
			after: `
library l;
resource table T {};
`,
			expected: `
api_diff:
- name: l/T
  before: "table l/T"
  after: "resource table l/T"
  conclusion: APIBreaking
`,
		},
		{
			name: "table unbecome resource",
			before: `
library l;
resource table T {};
`,
			after: `
library l;
table T {};
`,
			expected: `
api_diff:
- name: l/T
  before: "resource table l/T"
  after: "table l/T"
  conclusion: APIBreaking
`,
		},
		{
			name: "table add member",
			before: `
library l;
table T {};
`,
			after: `
library l;
table T {
  1: int32 foo;
};
`,
			expected: `
api_diff:
- name: l/T.foo
  after: "table/member l/T.foo int32"
  conclusion: SourceCompatible
`,
		},
		{
			name: "table remove member",
			before: `
library l;
table T {
  1: int32 foo;
};
`,
			after: `
library l;
table T {
};
`,
			expected: `
api_diff:
- name: l/T.foo
  before: "table/member l/T.foo int32"
  conclusion: APIBreaking
`,
		},
		{
			name: "table make member reserved",
			before: `
library l;
table T {
  1: int32 foo;
  2: int32 bar;
};
`,
			after: `
library l;
table T {
  1: reserved;
  2: int32 bar;
};
`,
			expected: `
api_diff:
- name: l/T.foo
  before: "table/member l/T.foo int32"
  conclusion: APIBreaking
`,
		},
		{
			name: "table change type",
			before: `
library l;
table T {
  1: int32 foo;
};
`,
			after: `
library l;
table T {
  1: string foo;
};
`,
			expected: `
api_diff:
- name: l/T.foo
  before: "table/member l/T.foo int32"
  after: "table/member l/T.foo string"
  conclusion: APIBreaking
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
union T {
  1: int32 foo;
};
`,
			expected: `
api_diff:
- name: l/T.foo
  after: "union/member l/T.foo int32"
  conclusion: SourceCompatible
- name: l/T
  after: "strict union l/T"
  conclusion: SourceCompatible
`,
		},
		{
			name: "union remove",
			before: `
library l;
union T {
  1: int32 foo;
};
`,
			after: `
library l;
`,
			expected: `
api_diff:
- name: l/T.foo
  before: "union/member l/T.foo int32"
  conclusion: APIBreaking
- name: l/T
  before: "strict union l/T"
  conclusion: APIBreaking
`,
		},
		{
			name: "union make reserved",
			before: `
library l;
union T {
  1: int32 foo;
  2: int32 bar;
};
`,
			after: `
library l;
union T {
  1: reserved;
  2: int32 bar;
};
`,
			expected: `
api_diff:
- name: l/T.foo
  before: "union/member l/T.foo int32"
  conclusion: APIBreaking
`,
		},
		{
			name: "union add reserved",
			before: `
library l;
union T {
  1: reserved;
  2: int32 foo;
};
`,
			after: `
library l;
union T {
  1: reserved;
  2: int32 foo;
  3: reserved;
};
`,
			expected: `
api_diff:
`,
		},
		{
			name: "union remove with reserved",
			before: `
library l;
union T {
  1: reserved;
  2: int32 foo;
};
`,
			after: `
library l;
`,
			expected: `
api_diff:
- name: l/T.foo
  before: "union/member l/T.foo int32"
  conclusion: APIBreaking
- name: l/T
  before: "strict union l/T"
  conclusion: APIBreaking
`,
		},
		{
			name: "union become resource",
			before: `
library l;
union T {
  1: int32 foo;
};
`,
			after: `
library l;
resource union T {
  1: int32 foo;
};
`,
			expected: `
api_diff:
- name: l/T
  before: "strict union l/T"
  after: "resource strict union l/T"
  conclusion: APIBreaking
`,
		},
		{
			name: "union unbecome resource",
			before: `
library l;
resource union T {
  1: int32 bar;
};
`,
			after: `
library l;
union T {
  1: int32 bar;
};
`,
			expected: `
api_diff:
- name: l/T
  before: "resource strict union l/T"
  after: "strict union l/T"
  conclusion: APIBreaking
`,
		},
		{
			name: "flexible union add member",
			before: `
library l;
flexible union T {
  1: int32 bar;
};
`,
			after: `
library l;
flexible union T {
  1: int32 bar;
  2: int32 foo;
};
`,
			expected: `
api_diff:
- name: l/T.foo
  after: "union/member l/T.foo int32"
  conclusion: SourceCompatible
`,
		},
		{
			name: "strict union add member",
			before: `
library l;
union T {
  1: int32 bar;
};
`,
			after: `
library l;
union T {
  1: int32 bar;
  2: int32 foo;
};
`,
			expected: `
api_diff:
- name: l/T.foo
  after: "union/member l/T.foo int32"
  conclusion: APIBreaking
`,
		},
		{
			name: "strict union remove member",
			before: `
library l;
union T {
  1: int32 bar;
  2: int32 foo;
};
`,
			after: `
library l;
union T {
  1: int32 bar;
};
`,
			expected: `
api_diff:
- name: l/T.foo
  before: "union/member l/T.foo int32"
  conclusion: APIBreaking
`,
		},
		{
			name: "union change type",
			before: `
library l;
union T {
  1: int32 foo;
};
`,
			after: `
library l;
union T {
  1: string foo;
};
`,
			expected: `
api_diff:
- name: l/T.foo
  before: "union/member l/T.foo int32"
  after: "union/member l/T.foo string"
  conclusion: APIBreaking
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
api_diff:
- name: l/T
  after: "protocol l/T"
  conclusion: SourceCompatible
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
api_diff:
- name: l/T
  before: "protocol l/T"
  conclusion: APIBreaking
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
  Test(int32 t) -> ();
};
`,
			expected: `
api_diff:
- name: l/T.Test
  after: "protocol/member l/T.Test (int32 t) -> ()"
  conclusion: Transitionable
`,
		},
		{
			name: "protocol member remove",
			before: `
library l;
protocol T {
  Test(int32 t) -> ();
};
`,
			after: `
library l;
protocol T {
};
`,
			expected: `
api_diff:
- name: l/T.Test
  before: "protocol/member l/T.Test (int32 t) -> ()"
  conclusion: APIBreaking
`,
		},
		{
			name: "protocol member type change",
			before: `
library l;
protocol T {
  Test(int32 t) -> ();
};
`,
			after: `
library l;
protocol T {
  Test(int32 t, int32 u) -> ();
};
`,
			expected: `
api_diff:
- name: l/T.Test
  before: "protocol/member l/T.Test (int32 t) -> ()"
  after: "protocol/member l/T.Test (int32 t,int32 u) -> ()"
  conclusion: APIBreaking
`,
		},
		// aliases don't appear in summaries, apparently.
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
			report, err := Compute(summaries[0], summaries[1])
			if err != nil {
				t.Fatalf("while computing diff: %v", err)
			}
			var buf strings.Builder
			if err := report.WriteText(&buf); err != nil {
				t.Fatalf("while writing test diff: %v", err)
			}
			actual, err := readTextReport(strings.NewReader(buf.String()))
			if err != nil {
				t.Fatalf("unexpected error while reading actual data: %v:\n%v", err, buf.String())
			}
			expected, err := readTextReport(strings.NewReader(test.expected))
			if err != nil {
				t.Fatalf("unexpected error while reading expected data: %v", err)
			}
			if !cmp.Equal(expected, actual, cmpOptions) {
				t.Errorf("want:\n\t%+v\n\tgot:\n\t%+v\n\tdiff:\n\t%v",
					expected, actual, cmp.Diff(expected, actual, cmpOptions))
			}
		})
	}
}

func summarizeOne(t *testing.T, r fidlgen.Root) string {
	t.Helper()
	var buf strings.Builder
	if err := summarize.WriteJSON(r, &buf); err != nil {
		t.Fatalf("error while summarizing: %v", err)
	}
	return buf.String()
}
