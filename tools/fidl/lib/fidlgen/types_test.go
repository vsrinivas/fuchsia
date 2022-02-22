// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_test

import (
	"encoding/json"
	"math"
	"reflect"
	"sort"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgentest"
)

// toDocComment formats doc comments in a by adding a leading space and a
// trailing newline.
func toDocComment(input string) string {
	return " " + input + "\n"
}

// Compares two fidlgen.Locations lexicographically on filename and then on
// the location within the file.
func LocationCmp(a, b *fidlgen.Location) bool {
	if cmp := strings.Compare(a.Filename, b.Filename); cmp != 0 {
		return cmp < 0
	}
	if a.Line != b.Line {
		return a.Line < b.Line
	}
	if a.Column != b.Column {
		return a.Column < b.Column
	}
	return a.Length < b.Length
}

func TestCanUnmarshalLargeOrdinal(t *testing.T) {
	input := `{
		"ordinal": 18446744073709551615
	}`

	var method fidlgen.Method
	err := json.Unmarshal([]byte(input), &method)
	if err != nil {
		t.Fatalf("failed to unmarshal: %s", err)
	}
	if method.Ordinal != math.MaxUint64 {
		t.Fatalf("method.Ordinal: expected math.MaxUint64, found %d", method.Ordinal)
	}
}

func TestCanUnmarshalAttributeValue(t *testing.T) {
	root := fidlgentest.EndToEndTest{T: t}.Single(`
		library example;

		@doc("MyUnion")
		@UpperCamelCase
		@lower_snake_case
		@CAPS
		type MyUnion = flexible union {
			/// my_union_member
			@on_the_member
			1: my_union_member bool;
		};
	`)

	// MyUnion
	unionName := string(root.Unions[0].Name)
	wantUnionName := "MyUnion"
	unionAttrs := root.Unions[0].Attributes
	if len(unionAttrs.Attributes) != 4 {
		t.Errorf("got %d attributes on '%s', want %d", len(unionAttrs.Attributes), unionName, 4)
	}

	attr, found := unionAttrs.LookupAttribute("doc")
	if !found {
		t.Errorf("'%s' 'doc' attribute: not found", unionName)
	}
	if arg, found := attr.LookupArgStandalone(); !found || arg.ValueString() != wantUnionName {
		t.Errorf("'%s' 'doc' attribute: got '%s', want '%s'", unionName, arg.ValueString(), wantUnionName)
	}
	if _, found := unionAttrs.LookupAttribute("UpperCamelCase"); !found {
		t.Errorf("'%s' 'UpperCamelCase' attribute: not found (using UpperCamelCase search)", unionName)
	}
	if _, found := unionAttrs.LookupAttribute("upper_camel_case"); !found {
		t.Errorf("'%s' 'UpperCamelCase' attribute: not found (using lower-case search)", unionName)
	}
	if _, found := unionAttrs.LookupAttribute("LowerSnakeCase"); !found {
		t.Errorf("'%s' 'lower_snake_case' attribute: not found (using UpperCamelCase search)", unionName)
	}
	if _, found := unionAttrs.LookupAttribute("lower_snake_case"); !found {
		t.Errorf("'%s' 'lower_snake_case' attribute: not found (using lower-case search)", unionName)
	}
	if _, found := unionAttrs.LookupAttribute("Caps"); !found {
		t.Errorf("'%s' 'CAPS' attribute: not found (using UpperCamelCase search)", unionName)
	}
	if _, found := unionAttrs.LookupAttribute("caps"); !found {
		t.Errorf("'%s' 'CAPS' attribute: not found (using lower-snake-case search)", unionName)
	}
	if _, found := unionAttrs.LookupAttribute("CAPS"); !found {
		t.Errorf("'%s' 'CAPS' attribute: not found (using default string for search)", unionName)
	}

	// my_union_member
	unionMemName := string(root.Unions[0].Members[0].Name)
	wantUnionMemName := "my_union_member"
	unionMemAttrs := root.Unions[0].Members[0].Attributes
	if unionMemName != wantUnionMemName {
		t.Errorf("got union with name '%s', want '%s'", unionMemName, wantUnionMemName)
	}
	if len(unionMemAttrs.Attributes) != 2 {
		t.Errorf("got %d attributes on '%s', want %d", len(unionMemAttrs.Attributes), unionMemName, 2)
	}

	attr, found = unionMemAttrs.LookupAttribute("doc")
	if !found {
		t.Errorf("'%s' 'doc' attribute: not found", unionMemName)
	}
	if arg, found := attr.LookupArgStandalone(); !found || arg.ValueString() != toDocComment(wantUnionMemName) {
		t.Errorf("'%s' 'doc' attribute: got '%s', want '%s'", unionMemName, arg.ValueString(), toDocComment(wantUnionMemName))
	}
	if _, found := unionMemAttrs.LookupAttribute("on_the_member"); !found {
		t.Errorf("'%s' 'on_the_member' attribute: not found", unionMemName)
	}
	if _, found := unionMemAttrs.LookupAttribute("Missing"); found {
		t.Errorf("'%s' 'missing' attribute: found when non-existant", unionMemName)
	}
	if _, found := unionMemAttrs.LookupAttribute("missing"); found {
		t.Errorf("'%s' 'missing' attribute: found when non-existant", unionMemName)
	}
}

func TestCanUnmarshalSignedEnums(t *testing.T) {

	root := fidlgentest.EndToEndTest{T: t}.Single(`
		library example;

		type StrictEnum = strict enum : uint8 {
			FALSE = 0;
			TRUE = 1;
		};

		type EmptyEnum = flexible enum : uint16 {};
		
		type FlexibleEnum = flexible enum : int64 {
			FIRST = 1;
			SECOND = 2;
		};
		
		type FlexibleEnumWithPlaceholder = flexible enum : uint32 {
			FIRST = 1;
			SECOND = 2;
			@unknown
			PLACEHOLDER = 3;
		};
	`)

	// Sort by location for easier comparison with expected values.
	sort.Slice(root.Enums, func(i, j int) bool {
		return LocationCmp(&root.Enums[i].Location, &root.Enums[j].Location)
	})

	expected := []fidlgen.Enum{
		{
			Layout: fidlgen.Layout{
				Decl: fidlgen.Decl{
					Name: "example/StrictEnum",
				},
			},
			Type: fidlgen.Uint8,
			Members: []fidlgen.EnumMember{
				{
					Name: "FALSE",
					Value: fidlgen.Constant{
						Kind: fidlgen.LiteralConstant,
						Literal: fidlgen.Literal{
							Kind:  fidlgen.NumericLiteral,
							Value: "0",
						},
						Value: "0",
					},
				},
				{
					Name: "TRUE",
					Value: fidlgen.Constant{
						Kind: fidlgen.LiteralConstant,
						Literal: fidlgen.Literal{
							Kind:  fidlgen.NumericLiteral,
							Value: "1",
						},
						Value: "1",
					},
				},
			},
			Strictness:      fidlgen.IsStrict,
			RawUnknownValue: fidlgen.Int64OrUint64FromUint64ForTesting(0),
		},
		{
			Layout: fidlgen.Layout{
				Decl: fidlgen.Decl{
					Name: "example/EmptyEnum",
				},
			},
			Type:            fidlgen.Uint16,
			Members:         []fidlgen.EnumMember{},
			Strictness:      fidlgen.IsFlexible,
			RawUnknownValue: fidlgen.Int64OrUint64FromUint64ForTesting(math.MaxUint16),
		},
		{
			Layout: fidlgen.Layout{
				Decl: fidlgen.Decl{
					Name: "example/FlexibleEnum",
				},
			},
			Type: fidlgen.Int64,
			Members: []fidlgen.EnumMember{
				{
					Name: "FIRST",
					Value: fidlgen.Constant{
						Kind: fidlgen.LiteralConstant,
						Literal: fidlgen.Literal{
							Kind:  fidlgen.NumericLiteral,
							Value: "1",
						},
						Value: "1",
					},
				},
				{
					Name: "SECOND",
					Value: fidlgen.Constant{
						Kind: fidlgen.LiteralConstant,
						Literal: fidlgen.Literal{
							Kind:  fidlgen.NumericLiteral,
							Value: "2",
						},
						Value: "2",
					},
				},
			},
			Strictness:      fidlgen.IsFlexible,
			RawUnknownValue: fidlgen.Int64OrUint64FromInt64ForTesting(math.MaxInt64),
		},
		{
			Layout: fidlgen.Layout{
				Decl: fidlgen.Decl{
					Name: "example/FlexibleEnumWithPlaceholder",
				},
			},
			Type: fidlgen.Uint32,
			Members: []fidlgen.EnumMember{
				{
					Name: "FIRST",
					Value: fidlgen.Constant{
						Kind: fidlgen.LiteralConstant,
						Literal: fidlgen.Literal{
							Kind:  fidlgen.NumericLiteral,
							Value: "1",
						},
						Value: "1",
					},
				},
				{
					Name: "SECOND",
					Value: fidlgen.Constant{
						Kind: fidlgen.LiteralConstant,
						Literal: fidlgen.Literal{
							Kind:  fidlgen.NumericLiteral,
							Value: "2",
						},
						Value: "2",
					},
				},
				{
					Attributes: fidlgen.Attributes{
						[]fidlgen.Attribute{
							{
								Name: fidlgen.Identifier("unknown"),
								Args: []fidlgen.AttributeArg{},
							},
						},
					},
					Name: "PLACEHOLDER",
					Value: fidlgen.Constant{
						Kind: fidlgen.LiteralConstant,
						Literal: fidlgen.Literal{
							Kind:  fidlgen.NumericLiteral,
							Value: "3",
						},
						Value: "3",
					},
				},
			},
			Strictness:      fidlgen.IsFlexible,
			RawUnknownValue: fidlgen.Int64OrUint64FromUint64ForTesting(3),
		},
	}

	if len(root.Enums) != len(expected) {
		t.Fatalf("unexpected number of enum declarations")
	}

	// RawUnknownValue has an unexported type; reflect into it to compare.
	opt := cmp.Exporter(func(t reflect.Type) bool {
		return t == reflect.TypeOf(fidlgen.Enum{}.RawUnknownValue)
	})

	for i, actual := range root.Enums {
		// Sanitize Location and NamindContext values, as they're not relevant here.
		actual.Layout.Decl.Location = fidlgen.Location{}
		actual.Layout.NamingContext = nil
		if diff := cmp.Diff(actual, expected[i], opt); len(diff) > 0 {
			t.Errorf("\nexpected: %#v\nactual: %#v\n", expected[i], actual)
		}
	}
}

func TestCanUnmarshalBits(t *testing.T) {
	root := fidlgentest.EndToEndTest{T: t}.Single(`
		library example;

		type Perms = flexible bits : uint8 {
			R = 0b0001;
			W = 0b0010;
			X = 0b0100;
		};

		type StrictBits = strict bits : uint64 {
			SMALLEST = 1;
			BIGGEST = 0x8000000000000000;
		};
	`)

	// Sort by location for easier comparison with expected values.
	sort.Slice(root.Bits, func(i, j int) bool {
		return LocationCmp(&root.Bits[i].Location, &root.Bits[j].Location)
	})

	uint8Shape := fidlgen.TypeShape{
		InlineSize: 1,
		Alignment:  1,
	}

	uint64Shape := fidlgen.TypeShape{
		InlineSize: 8,
		Alignment:  8,
	}

	expected := []fidlgen.Bits{
		{
			Layout: fidlgen.Layout{
				Decl: fidlgen.Decl{
					Name: "example/Perms",
				},
			},
			Type: fidlgen.Type{
				Kind:             fidlgen.PrimitiveType,
				PrimitiveSubtype: fidlgen.Uint8,
				TypeShapeV1:      uint8Shape,
				TypeShapeV2:      uint8Shape,
			},
			Mask: "7",
			Members: []fidlgen.BitsMember{
				{
					Name: "R",
					Value: fidlgen.Constant{
						Kind: fidlgen.LiteralConstant,
						Literal: fidlgen.Literal{
							Kind:  fidlgen.NumericLiteral,
							Value: "1",
						},
						Value: "1",
					},
				},
				{
					Name: "W",
					Value: fidlgen.Constant{
						Kind: fidlgen.LiteralConstant,
						Literal: fidlgen.Literal{
							Kind:  fidlgen.NumericLiteral,
							Value: "2",
						},
						Value: "2",
					},
				},
				{
					Name: "X",
					Value: fidlgen.Constant{
						Kind: fidlgen.LiteralConstant,
						Literal: fidlgen.Literal{
							Kind:  fidlgen.NumericLiteral,
							Value: "4",
						},
						Value: "4",
					},
				},
			},
			Strictness: fidlgen.IsFlexible,
		},
		{
			Layout: fidlgen.Layout{
				Decl: fidlgen.Decl{
					Name: "example/StrictBits",
				},
			},
			Type: fidlgen.Type{
				Kind:             fidlgen.PrimitiveType,
				PrimitiveSubtype: fidlgen.Uint64,
				TypeShapeV1:      uint64Shape,
				TypeShapeV2:      uint64Shape,
			},
			Mask: "9223372036854775809",
			Members: []fidlgen.BitsMember{
				{
					Name: "SMALLEST",
					Value: fidlgen.Constant{
						Kind: fidlgen.LiteralConstant,
						Literal: fidlgen.Literal{
							Kind:  fidlgen.NumericLiteral,
							Value: "1",
						},
						Value: "1",
					},
				},
				{
					Name: "BIGGEST",
					Value: fidlgen.Constant{
						Kind: fidlgen.LiteralConstant,
						Literal: fidlgen.Literal{
							Kind:  fidlgen.NumericLiteral,
							Value: "9223372036854775808",
						},
						Value: "9223372036854775808",
					},
				},
			},
			Strictness: fidlgen.IsStrict,
		},
	}

	if len(root.Bits) != len(expected) {
		t.Fatalf("unexpected number of bits declarations")
	}

	for i, actual := range root.Bits {
		// Sanitize Location and NamindContext values, as they're not relevant here.
		actual.Layout.Decl.Location = fidlgen.Location{}
		actual.Layout.NamingContext = nil
		if diff := cmp.Diff(actual, expected[i]); len(diff) > 0 {
			t.Errorf("\nexpected: %#v\nactual: %#v\n", expected[i], actual)
		}
	}
}

func TestCanUnmarshalLocation(t *testing.T) {
	root := fidlgentest.EndToEndTest{T: t}.Single(`
		library example;

		const CONST_A bool = false;

		      const CONST_B uint32 = 10;		
	`)

	// Sort by location for easier comparison with expected values.
	sort.Slice(root.Consts, func(i, j int) bool {
		return LocationCmp(&root.Consts[i].Location, &root.Consts[j].Location)
	})

	expected := []fidlgen.Const{
		{
			Decl: fidlgen.Decl{
				Name: "example/CONST_A",
				Location: fidlgen.Location{
					Line:   4,
					Column: 9,
					Length: 7,
				},
			},
		},
		{
			Decl: fidlgen.Decl{
				Name: "example/CONST_B",
				Location: fidlgen.Location{
					Line:   6,
					Column: 15,
					Length: 7,
				},
			},
		},
	}

	if len(root.Consts) != len(expected) {
		t.Fatalf("unexpected number of constant declarations")
	}

	for i, actual := range root.Consts {
		if actual.Location.Filename == "" {
			t.Errorf("Constant %s has empty file location", actual.Name)
		}

		// Sanitize Location.Filename, Value, and Type values, as they're not relevant here.
		actual.Location.Filename = ""
		actual.Value = fidlgen.Constant{}
		actual.Type = fidlgen.Type{}
		if diff := cmp.Diff(actual, expected[i]); len(diff) > 0 {
			t.Errorf("\nexpected: %#v\nactual: %#v\n", expected[i], actual)
		}
	}
}

func TestCanUnmarshalTypeAliases(t *testing.T) {
	// TODO(fxbug.dev/91360): Exercise the associated, currently-broken corner
	// cases when possible.
	root := fidlgentest.EndToEndTest{T: t}.Single(`
		library example;

		alias UintThirtyTwo = uint32;
		alias Foo = vector<uint32>;
		alias FooVector = vector<vector<uint32>>;
		alias Bar = vector<bool>:optional;
		alias Baz = string:<123>;
	`)

	// Sort results by location for easier comparison.
	sort.Slice(root.TypeAliases, func(i, j int) bool {
		return LocationCmp(&root.TypeAliases[i].Location, &root.TypeAliases[j].Location)
	})

	expected := []fidlgen.TypeAlias{
		{
			Decl: fidlgen.Decl{
				Name: "example/UintThirtyTwo",
			},
			PartialTypeConstructor: fidlgen.PartialTypeConstructor{
				Name:     "uint32",
				Args:     []fidlgen.PartialTypeConstructor{},
				Nullable: false,
			},
		},
		{
			Decl: fidlgen.Decl{
				Name: "example/Foo",
			},
			PartialTypeConstructor: fidlgen.PartialTypeConstructor{
				Name: "vector",
				Args: []fidlgen.PartialTypeConstructor{
					{
						Name:     "uint32",
						Args:     []fidlgen.PartialTypeConstructor{},
						Nullable: false,
					},
				},
				Nullable: false,
			},
		},
		{
			Decl: fidlgen.Decl{
				Name: "example/FooVector",
			},
			PartialTypeConstructor: fidlgen.PartialTypeConstructor{
				Name: "vector",
				Args: []fidlgen.PartialTypeConstructor{
					{
						Name: "vector",
						Args: []fidlgen.PartialTypeConstructor{
							{
								Name:     "uint32",
								Args:     []fidlgen.PartialTypeConstructor{},
								Nullable: false,
							},
						},
						Nullable: false,
					},
				},
				Nullable: false,
			},
		},
		{
			Decl: fidlgen.Decl{
				Name: "example/Bar",
			},
			PartialTypeConstructor: fidlgen.PartialTypeConstructor{
				Name: "vector",
				Args: []fidlgen.PartialTypeConstructor{
					{
						Name:     "bool",
						Args:     []fidlgen.PartialTypeConstructor{},
						Nullable: false,
					},
				},
				Nullable: true,
			},
		},
		{
			Decl: fidlgen.Decl{
				Name: "example/Baz",
			},
			PartialTypeConstructor: fidlgen.PartialTypeConstructor{
				Name:     "string",
				Args:     []fidlgen.PartialTypeConstructor{},
				Nullable: false,
				MaybeSize: &fidlgen.Constant{
					Kind: fidlgen.LiteralConstant,
					Literal: fidlgen.Literal{
						Kind:  fidlgen.NumericLiteral,
						Value: "123",
					},
					Value: "123",
				},
			},
		},
	}

	if len(root.TypeAliases) != len(expected) {
		t.Fatalf("unexpected number of type aliases")
	}

	for i, actual := range root.TypeAliases {
		actual.Decl.Location = fidlgen.Location{} // Does not matter for the purpose of this comparison.
		if diff := cmp.Diff(actual, expected[i]); len(diff) > 0 {
			t.Errorf("\nexpected: %#v\nactual: %#v", expected[i], actual)
		}
	}
}

func TestEncodedCompoundIdentifierParsing(t *testing.T) {
	type testCase struct {
		input          fidlgen.EncodedCompoundIdentifier
		expectedOutput fidlgen.CompoundIdentifier
	}
	tests := []testCase{
		{
			input:          "Decl",
			expectedOutput: compoundIdentifier([]string{""}, "Decl", ""),
		},
		{
			input:          "fuchsia.some.library/Decl",
			expectedOutput: compoundIdentifier([]string{"fuchsia", "some", "library"}, "Decl", ""),
		},
		{
			input:          "Name.MEMBER",
			expectedOutput: compoundIdentifier([]string{""}, "Name", "MEMBER"),
		},

		{
			input:          "fuchsia.some.library/Decl.MEMBER",
			expectedOutput: compoundIdentifier([]string{"fuchsia", "some", "library"}, "Decl", "MEMBER"),
		},
	}
	for _, test := range tests {
		output := test.input.Parse()
		diff := cmp.Diff(output, test.expectedOutput)
		if len(diff) > 0 {
			t.Errorf("unexpected output for input %q diff: %s", test.input, diff)
		}
	}
}

func compoundIdentifier(library []string, name, member string) fidlgen.CompoundIdentifier {
	var convertedLibrary fidlgen.LibraryIdentifier
	for _, part := range library {
		convertedLibrary = append(convertedLibrary, fidlgen.Identifier(part))
	}
	return fidlgen.CompoundIdentifier{
		Library: convertedLibrary,
		Name:    fidlgen.Identifier(name),
		Member:  fidlgen.Identifier(member),
	}
}
