// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package zither

import (
	"fmt"
	"sort"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgentest"
)

// Permits the comparison of types with unexported fields.
var cmpOpt = cmp.AllowUnexported(
	fidlgen.LibraryName{},
	fidlgen.Name{},
	Const{},
	Enum{},
	EnumMember{},
	Bits{},
	BitsMember{},
	Struct{},
	StructMember{},
	Alias{},
	SyscallFamily{},
	Syscall{},
)

func TestGeneratedFileCount(t *testing.T) {
	{
		wd := t.TempDir()
		ir := fidlgentest.EndToEndTest{T: t}.WithWorkingDirectory(wd).Single(`
	library example;

	const A bool = true;
	`)

		summaries, err := Summarize(ir, wd, SourceDeclOrder)
		if err != nil {
			t.Fatal(err)
		}
		if len(summaries) != 1 {
			t.Fatalf("expected one summary; got %d", len(summaries))
		}
	}

	{
		wd := t.TempDir()
		ir := fidlgentest.EndToEndTest{T: t}.WithWorkingDirectory(wd).Multiple([]string{
			`
	library example;

	const A bool = true;
	`,
			`
	library example;

	const B bool = true;
	`,
			`
	library example;

	const C bool = true;
	`,
		})

		summaries, err := Summarize(ir, wd, SourceDeclOrder)
		if err != nil {
			t.Fatal(err)
		}
		if len(summaries) != 3 {
			t.Fatalf("expected three summaries; got %d", len(summaries))
		}
	}
}

func TestCanSummarizeLibraryName(t *testing.T) {
	name := "this.is.an.example.library"
	wd := t.TempDir()
	ir := fidlgentest.EndToEndTest{T: t}.WithWorkingDirectory(wd).Single(fmt.Sprintf(`
	library %s;

	const A bool = true;
	`, name))

	summaries, err := Summarize(ir, wd, SourceDeclOrder)
	if err != nil {
		t.Fatal(err)
	}
	if summaries[0].Library.String() != name {
		t.Errorf("expected %s; got %s", name, summaries[0].Library)
	}
}

func TestDeclOrder(t *testing.T) {
	wd := t.TempDir()
	ir := fidlgentest.EndToEndTest{T: t}.WithWorkingDirectory(wd).Single(`
library example;

const A int32 = 0;
const B int32 = E;
const C int32 = A;
const D int32 = 1;
const E int32 = C;
const F int32 = B;
const G int32 = 2;
`)

	{
		summaries, err := Summarize(ir, wd, SourceDeclOrder)
		if err != nil {
			t.Fatal(err)
		}

		var actual []string
		for _, decl := range summaries[0].Decls {
			actual = append(actual, decl.Name().String())
		}
		expected := []string{
			"example/A",
			"example/B",
			"example/C",
			"example/D",
			"example/E",
			"example/F",
			"example/G",
		}
		if diff := cmp.Diff(expected, actual); diff != "" {
			t.Error(diff)
		}
	}

	{
		summaries, err := Summarize(ir, wd, DependencyDeclOrder)
		if err != nil {
			t.Fatal(err)
		}

		var actual []string
		for _, decl := range summaries[0].Decls {
			actual = append(actual, decl.Name().String())
		}
		expected := []string{
			"example/A",
			"example/C",
			"example/D", // C and D have no interdependencies, and D follows C in source.
			"example/E",
			"example/B",
			"example/F",
			"example/G",
		}
		if diff := cmp.Diff(expected, actual); diff != "" {
			t.Error(diff)
		}
	}
}

func TestFloatConstantsAreDisallowed(t *testing.T) {
	decls := []string{
		"const FLOAT32 float32 = 0.0;",
		"const FLOAT64 float64 = 0.0;",
	}

	for _, decl := range decls {
		wd := t.TempDir()
		ir := fidlgentest.EndToEndTest{T: t}.WithWorkingDirectory(wd).Single(fmt.Sprintf(`
library example;

%s
`, decl))

		_, err := Summarize(ir, wd, SourceDeclOrder)
		if err == nil {
			t.Fatal("expected an error")
		}
		if err.Error() != "floats are unsupported" {
			t.Errorf("unexpected error: %v", err)
		}
	}
}

func TestCanSummarizeConstants(t *testing.T) {
	wd := t.TempDir()
	ir := fidlgentest.EndToEndTest{T: t}.WithWorkingDirectory(wd).Single(`
library example;

const BOOL bool = false;

const BINARY_UINT8 uint8 = 0b10101111;

const HEX_UINT16 uint16 = 0xabcd;

const DECIMAL_UINT32 uint32 = 123456789;

const BINARY_INT8 int8 = 0b1111010;

const HEX_INT16 int16 = 0xcba;

const NEGATIVE_HEX_INT16 int16 = -0xcba;

const DECIMAL_INT32 int32 = 1050065;

const NEGATIVE_DECIMAL_INT32 int32 = -1050065;

const UINT64_MAX uint64 = 0xffffffffffffffff;

const INT64_MIN int64 = -0x8000000000000000;

const SOME_STRING string = "XXX";

const DEFINED_IN_TERMS_OF_ANOTHER_STRING string = SOME_STRING;

const DEFINED_IN_TERMS_OF_ANOTHER_UINT16 uint16 = HEX_UINT16;

type Uint8Enum = strict enum : uint8 {
	MAX = 0xff;
};

const UINT8_ENUM_VALUE Uint8Enum = Uint8Enum.MAX;

/// This is a one-line comment.
const COMMENTED_BOOL bool = true;

/// This is
///   a
///       many-line
/// comment.
const COMMENTED_STRING string = "YYY";
`)
	summaries, err := Summarize(ir, wd, SourceDeclOrder)
	if err != nil {
		t.Fatal(err)
	}

	var actual []Const
	for _, decl := range summaries[0].Decls {
		if decl.IsConst() {
			actual = append(actual, decl.AsConst())
		}
	}

	someString := Const{
		decl:  decl{Name: fidlgen.MustReadName("example/SOME_STRING")},
		Kind:  TypeKindString,
		Type:  "string",
		Value: "XXX",
	}

	hexUint16 := Const{
		decl:  decl{Name: fidlgen.MustReadName("example/HEX_UINT16")},
		Kind:  TypeKindInteger,
		Type:  "uint16",
		Value: "0xabcd",
	}

	uint8Enum := Enum{
		decl:    decl{Name: fidlgen.MustReadName("example/Uint8Enum")},
		Subtype: "uint8",
		Members: []EnumMember{
			{
				member: member{Name: "MAX"},
				Value:  "0xff",
			},
		},
	}

	// Listed in declaration order for readability, but similarly sorted.
	expected := []Const{
		{
			decl:  decl{Name: fidlgen.MustReadName("example/BOOL")},
			Kind:  TypeKindBool,
			Type:  "bool",
			Value: "false",
		},
		{
			decl:  decl{Name: fidlgen.MustReadName("example/BINARY_UINT8")},
			Kind:  TypeKindInteger,
			Type:  "uint8",
			Value: "0b10101111",
		},
		hexUint16,
		{
			decl:  decl{Name: fidlgen.MustReadName("example/DECIMAL_UINT32")},
			Kind:  TypeKindInteger,
			Type:  "uint32",
			Value: "123456789",
		},
		{
			decl:  decl{Name: fidlgen.MustReadName("example/BINARY_INT8")},
			Kind:  TypeKindInteger,
			Type:  "int8",
			Value: "0b1111010",
		},
		{
			decl:  decl{Name: fidlgen.MustReadName("example/HEX_INT16")},
			Kind:  TypeKindInteger,
			Type:  "int16",
			Value: "0xcba",
		},
		{
			decl:  decl{Name: fidlgen.MustReadName("example/NEGATIVE_HEX_INT16")},
			Kind:  TypeKindInteger,
			Type:  "int16",
			Value: "-0xcba",
		},
		{
			decl:  decl{Name: fidlgen.MustReadName("example/DECIMAL_INT32")},
			Kind:  TypeKindInteger,
			Type:  "int32",
			Value: "1050065",
		},
		{
			decl:  decl{Name: fidlgen.MustReadName("example/NEGATIVE_DECIMAL_INT32")},
			Kind:  TypeKindInteger,
			Type:  "int32",
			Value: "-1050065",
		},
		{
			decl:  decl{Name: fidlgen.MustReadName("example/UINT64_MAX")},
			Kind:  TypeKindInteger,
			Type:  "uint64",
			Value: "0xffffffffffffffff",
		},
		{
			decl:  decl{Name: fidlgen.MustReadName("example/INT64_MIN")},
			Kind:  TypeKindInteger,
			Type:  "int64",
			Value: "-0x8000000000000000",
		},
		someString,
		{
			decl:    decl{Name: fidlgen.MustReadName("example/DEFINED_IN_TERMS_OF_ANOTHER_STRING")},
			Kind:    TypeKindString,
			Type:    "string",
			Value:   "XXX",
			Element: &ConstElementValue{Decl: &someString},
		},
		{
			decl:    decl{Name: fidlgen.MustReadName("example/DEFINED_IN_TERMS_OF_ANOTHER_UINT16")},
			Kind:    TypeKindInteger,
			Type:    "uint16",
			Value:   "43981",
			Element: &ConstElementValue{Decl: &hexUint16},
		},
		{

			decl:  decl{Name: fidlgen.MustReadName("example/UINT8_ENUM_VALUE")},
			Kind:  TypeKindEnum,
			Type:  "example/Uint8Enum",
			Value: "255",
			Element: &ConstElementValue{
				Decl:   &uint8Enum,
				Member: uint8Enum.Members[0],
			},
		},
		{
			decl: decl{
				Name:     fidlgen.MustReadName("example/COMMENTED_BOOL"),
				Comments: []string{" This is a one-line comment."},
			},
			Kind:  TypeKindBool,
			Type:  "bool",
			Value: "true",
		},
		{
			decl: decl{
				Name:     fidlgen.MustReadName("example/COMMENTED_STRING"),
				Comments: []string{" This is", "   a", "       many-line", " comment."},
			},
			Kind:  TypeKindString,
			Type:  "string",
			Value: "YYY",
		},
	}

	if diff := cmp.Diff(expected, actual, cmpOpt); diff != "" {
		t.Error(diff)
	}
}

func TestCanSummarizeEnums(t *testing.T) {
	wd := t.TempDir()
	ir := fidlgentest.EndToEndTest{T: t}.WithWorkingDirectory(wd).Single(`
library example;

/// This is a uint8 enum.
type Uint8Enum = enum : uint8 {
  /// This is a member.
  TWO = 0b10;

  /// This is
  /// another
  /// member.
  SEVENTEEN = 17;
};

/// This
/// is
/// an
/// int64 enum.
type Int64Enum = enum : int64 {
  MINUS_HEX_ABCD = -0xabcd;
  HEX_DEADBEEF = 0xdeadbeef;
};
`)
	summaries, err := Summarize(ir, wd, SourceDeclOrder)
	if err != nil {
		t.Fatal(err)
	}

	// Normalize member order by name for a stable comparison.
	var actual []Enum
	for _, decl := range summaries[0].Decls {
		if decl.IsEnum() {
			enum := decl.AsEnum()
			sort.Slice(enum.Members, func(i, j int) bool {
				return strings.Compare(enum.Members[i].Name, enum.Members[j].Name) < 0
			})
			actual = append(actual, enum)
		}
	}

	expected := []Enum{
		{
			decl: decl{
				Name:     fidlgen.MustReadName("example/Uint8Enum"),
				Comments: []string{" This is a uint8 enum."},
			},
			Subtype: "uint8",
			Members: []EnumMember{
				{
					member: member{
						Name:     "SEVENTEEN",
						Comments: []string{" This is", " another", " member."},
					},
					Value: "17",
				},
				{
					member: member{
						Name:     "TWO",
						Comments: []string{" This is a member."},
					},
					Value: "0b10",
				},
			},
		},
		{
			Subtype: "int64",
			decl: decl{
				Name:     fidlgen.MustReadName("example/Int64Enum"),
				Comments: []string{" This", " is", " an", " int64 enum."},
			},
			Members: []EnumMember{
				{
					member: member{Name: "HEX_DEADBEEF"},
					Value:  "0xdeadbeef",
				},
				{
					member: member{Name: "MINUS_HEX_ABCD"},
					Value:  "-0xabcd",
				},
			},
		},
	}

	if diff := cmp.Diff(expected, actual, cmpOpt); diff != "" {
		t.Error(diff)
	}
}

func TestCanSummarizeBits(t *testing.T) {
	wd := t.TempDir()
	ir := fidlgentest.EndToEndTest{T: t}.WithWorkingDirectory(wd).Single(`
library example;

/// This is a uint8 bits.
type Uint8Bits = bits : uint8 {
  /// This is a member.
  ONE = 0b1;

  /// This is
  /// another
  /// member.
  SIXTEEN = 16;
};

/// This
/// is
/// a
/// uint64 bits.
type Uint64Bits = bits : uint64 {
  MEMBER = 0x1000;
};
`)
	summaries, err := Summarize(ir, wd, SourceDeclOrder)
	if err != nil {
		t.Fatal(err)
	}

	// Normalize member order by name for a stable comparison.
	var actual []Bits
	for _, decl := range summaries[0].Decls {
		if decl.IsBits() {
			bits := decl.AsBits()
			sort.Slice(bits.Members, func(i, j int) bool {
				return strings.Compare(bits.Members[i].Name, bits.Members[j].Name) < 0
			})
			actual = append(actual, bits)
		}
	}

	expected := []Bits{
		{
			Subtype: fidlgen.Uint8,
			decl: decl{
				Name:     fidlgen.MustReadName("example/Uint8Bits"),
				Comments: []string{" This is a uint8 bits."},
			},
			Members: []BitsMember{
				{
					member: member{
						Name:     "ONE",
						Comments: []string{" This is a member."},
					},
					Index: 0,
				},
				{
					member: member{
						Name:     "SIXTEEN",
						Comments: []string{" This is", " another", " member."},
					},
					Index: 4,
				},
			},
		},
		{
			Subtype: fidlgen.Uint64,
			decl: decl{
				Name:     fidlgen.MustReadName("example/Uint64Bits"),
				Comments: []string{" This", " is", " a", " uint64 bits."},
			},
			Members: []BitsMember{
				{
					member: member{Name: "MEMBER"},
					Index:  12,
				},
			},
		},
	}

	if diff := cmp.Diff(expected, actual, cmpOpt); diff != "" {
		t.Error(diff)
	}
}

func TestCanSummarizeStructs(t *testing.T) {
	wd := t.TempDir()
	ir := fidlgentest.EndToEndTest{T: t}.WithWorkingDirectory(wd).Single(`
library example;

/// This is a struct.
type EmptyStruct = struct {};

type BasicStruct = struct {
	/// This is a struct member.
    i64 int64;
    u64 uint64;
    i32 int32;
    u32 uint32;
    i16 int16;
    u16 uint16;
    i8 int8;
    u8 uint8;
    b bool;
	e Enum;
	bits Bits;
	empty EmptyStruct;
};

type Enum = enum : uint16 {
	ZERO = 0;
};

type Bits = bits : uint16 {
	ONE = 1;
};

type StructWithArrayMembers = struct {
    u8s array<uint8, 10>;
    empties array<EmptyStruct, 6>;
    nested array<array<bool, 2>, 4>;
};
`)
	summaries, err := Summarize(ir, wd, SourceDeclOrder)
	if err != nil {
		t.Fatal(err)
	}

	var actual []Struct
	for _, decl := range summaries[0].Decls {
		if decl.IsStruct() {
			actual = append(actual, decl.AsStruct())
		}
	}

	// Addressable integers for use as TypeDescriptor.ElementCount below.
	two, four, six, ten := 2, 4, 6, 10

	emptyStruct := Struct{
		decl: decl{
			Name:     fidlgen.MustReadName("example/EmptyStruct"),
			Comments: []string{" This is a struct."},
		},
		Size: 1,
	}

	enum := Enum{
		decl:    decl{Name: fidlgen.MustReadName("example/Enum")},
		Subtype: "uint16",
		Members: []EnumMember{
			{
				member: member{Name: "ZERO"},
				Value:  "0",
			},
		},
	}

	bits := Bits{
		decl:    decl{Name: fidlgen.MustReadName("example/Bits")},
		Subtype: "uint16",
		Members: []BitsMember{
			{
				member: member{Name: "ONE"},
				Index:  0,
			},
		},
	}

	expected := []Struct{
		emptyStruct,
		{
			decl: decl{
				Name: fidlgen.MustReadName("example/BasicStruct"),
			},
			Size: 40,
			Members: []StructMember{
				{
					member: member{
						Name:     "i64",
						Comments: []string{" This is a struct member."},
					},
					Type: TypeDescriptor{
						Type: "int64",
						Kind: TypeKindInteger,
					},
					Offset: 0,
				},
				{
					member: member{Name: "u64"},
					Type: TypeDescriptor{
						Type: "uint64",
						Kind: TypeKindInteger,
					},
					Offset: 8,
				},
				{
					member: member{Name: "i32"},
					Type: TypeDescriptor{
						Type: "int32",
						Kind: TypeKindInteger,
					},
					Offset: 16,
				},
				{
					member: member{Name: "u32"},
					Type: TypeDescriptor{
						Type: "uint32",
						Kind: TypeKindInteger,
					},
					Offset: 20,
				},
				{
					member: member{Name: "i16"},
					Type: TypeDescriptor{
						Type: "int16",
						Kind: TypeKindInteger,
					},
					Offset: 24,
				},
				{
					member: member{Name: "u16"},
					Type: TypeDescriptor{
						Type: "uint16",
						Kind: TypeKindInteger,
					},
					Offset: 26,
				},
				{
					member: member{Name: "i8"},
					Type: TypeDescriptor{
						Type: "int8",
						Kind: TypeKindInteger,
					},
					Offset: 28,
				},
				{
					member: member{Name: "u8"},
					Type: TypeDescriptor{
						Type: "uint8",
						Kind: TypeKindInteger,
					},
					Offset: 29,
				},
				{
					member: member{Name: "b"},
					Type: TypeDescriptor{
						Type: "bool",
						Kind: TypeKindBool,
					},
					Offset: 30,
				},
				{
					member: member{Name: "e"},
					Type: TypeDescriptor{
						Type: "example/Enum",
						Kind: TypeKindEnum,
						Decl: &enum,
					},
					Offset: 32,
				},
				{
					member: member{Name: "bits"},
					Type: TypeDescriptor{
						Type: "example/Bits",
						Kind: TypeKindBits,
						Decl: &bits,
					},
					Offset: 34,
				},
				{
					member: member{Name: "empty"},
					Type: TypeDescriptor{
						Type: "example/EmptyStruct",
						Kind: TypeKindStruct,
						Decl: &emptyStruct,
					},
					Offset: 36,
				},
			},
		},
		{
			decl: decl{Name: fidlgen.MustReadName("example/StructWithArrayMembers")},
			Size: 24,
			Members: []StructMember{
				{
					member: member{Name: "u8s"},
					Type: TypeDescriptor{
						Kind: TypeKindArray,
						ElementType: &TypeDescriptor{
							Type: "uint8",
							Kind: TypeKindInteger,
						},
						ElementCount: &ten,
					},
					Offset: 0,
				},
				{
					member: member{Name: "empties"},
					Type: TypeDescriptor{
						Kind: TypeKindArray,
						ElementType: &TypeDescriptor{
							Type: "example/EmptyStruct",
							Kind: TypeKindStruct,
							Decl: &emptyStruct,
						},
						ElementCount: &six,
					},
					Offset: 10,
				},
				{
					member: member{Name: "nested"},
					Type: TypeDescriptor{
						Kind: TypeKindArray,
						ElementType: &TypeDescriptor{
							Kind: TypeKindArray,
							ElementType: &TypeDescriptor{
								Type: "bool",
								Kind: TypeKindBool,
							},
							ElementCount: &two,
						},
						ElementCount: &four,
					},
					Offset: 16,
				},
			},
		},
	}

	if diff := cmp.Diff(expected, actual, cmpOpt); diff != "" {
		t.Error(diff)
	}
}

func TestCanSummarizeAliases(t *testing.T) {
	wd := t.TempDir()
	ir := fidlgentest.EndToEndTest{T: t}.WithWorkingDirectory(wd).Single(`
library example;

type Enum = enum : uint16 {
	ZERO = 0;
};

type Bits = bits : uint16 {
	ONE = 1;
};

type Struct = struct {
	value uint64;
};

/// This is an alias.
alias Uint8Alias = uint8;

alias EnumAlias = Enum;

alias BitsAlias = Bits;

alias StructAlias = Struct;

alias ArrayAlias = array<uint32, 4>;

alias NestedArrayAlias = array<array<Struct, 8>, 4>;

// TODO(fxbug.dev/105758, fxbug.dev/91360): Aliases are currently broken.
// Exercise more complicated aliases (e.g., aliases of aliases) when fixed.

`)
	summaries, err := Summarize(ir, wd, SourceDeclOrder)
	if err != nil {
		t.Fatal(err)
	}

	var actual []Alias
	for _, decl := range summaries[0].Decls {
		if decl.IsAlias() {
			actual = append(actual, decl.AsAlias())
		}
	}

	enum := Enum{
		decl:    decl{Name: fidlgen.MustReadName("example/Enum")},
		Subtype: "uint16",
		Members: []EnumMember{
			{
				member: member{Name: "ZERO"},
				Value:  "0",
			},
		},
	}

	bits := Bits{
		decl:    decl{Name: fidlgen.MustReadName("example/Bits")},
		Subtype: "uint16",
		Members: []BitsMember{
			{
				member: member{Name: "ONE"},
				Index:  0,
			},
		},
	}

	strct := Struct{
		decl: decl{Name: fidlgen.MustReadName("example/Struct")},
		Size: 8,
		Members: []StructMember{
			{
				member: member{Name: "value"},
				Offset: 0,
				Type: TypeDescriptor{
					Type: "uint64",
					Kind: TypeKindInteger,
				},
			},
		},
	}

	four, eight := 4, 8
	expected := []Alias{
		{
			decl: decl{
				Name:     fidlgen.MustReadName("example/Uint8Alias"),
				Comments: []string{" This is an alias."},
			},
			Value: TypeDescriptor{
				Type: "uint8",
				Kind: TypeKindInteger,
			},
		},
		{
			decl: decl{Name: fidlgen.MustReadName("example/EnumAlias")},
			Value: TypeDescriptor{
				Type: "example/Enum",
				Kind: TypeKindEnum,
				Decl: &enum,
			},
		},
		{
			decl: decl{Name: fidlgen.MustReadName("example/BitsAlias")},
			Value: TypeDescriptor{
				Type: "example/Bits",
				Kind: TypeKindBits,
				Decl: &bits,
			},
		},
		{
			decl: decl{Name: fidlgen.MustReadName("example/StructAlias")},
			Value: TypeDescriptor{
				Type: "example/Struct",
				Kind: TypeKindStruct,
				Decl: &strct,
			},
		},
		{
			decl: decl{Name: fidlgen.MustReadName("example/ArrayAlias")},
			Value: TypeDescriptor{
				Kind: TypeKindArray,
				ElementType: &TypeDescriptor{
					Type: "uint32",
					Kind: TypeKindInteger,
				},
				ElementCount: &four,
			},
		},
		{
			decl: decl{Name: fidlgen.MustReadName("example/NestedArrayAlias")},
			Value: TypeDescriptor{
				Kind: TypeKindArray,
				ElementType: &TypeDescriptor{
					Kind: TypeKindArray,
					ElementType: &TypeDescriptor{
						Type: "example/Struct",
						Kind: TypeKindStruct,
						Decl: &strct,
					},
					ElementCount: &eight,
				},
				ElementCount: &four,
			},
		},
	}

	if diff := cmp.Diff(expected, actual, cmpOpt); diff != "" {
		t.Error(diff)
	}
}

func TestCanSummarizeSyscalls(t *testing.T) {
	wd := t.TempDir()
	ir := fidlgentest.EndToEndTest{T: t}.WithWorkingDirectory(wd).Single(`
library example;

/// This gives the foo syscalls.
@transport("Syscall")
protocol Foo {
  /// This is the foo_bar syscall.
  Bar();
};

/// This
/// gives
/// the
/// fizz syscalls.
@no_protocol_prefix
@transport("Syscall")
protocol Arbitrary {
  /// This
  /// is
  /// the
  /// fizz_buzz syscall.
  FizzBuzz();
};

@transport("Syscall")
protocol Category {
	@vdsocall
	VdsoCall();

	@const
	@vdsocall
	ConstVdsoCall();

	@internal
	Internal();

    @next
    Next();

	@blocking
	Blocking();

	@noreturn
	NoReturn();

    @testonly
    Test0();

    @testonly
    @test_category1
    Test1();

    @testonly
    @test_category2
    Test2();
};

`)
	summaries, err := Summarize(ir, wd, SourceDeclOrder)
	if err != nil {
		t.Fatal(err)
	}

	// Normalize member order by name for a stable comparison.
	var actual []SyscallFamily
	for _, decl := range summaries[0].Decls {
		if decl.IsSyscallFamily() {
			fam := decl.AsSyscallFamily()
			sort.Slice(fam.Syscalls, func(i, j int) bool {
				return strings.Compare(fam.Syscalls[i].Name, fam.Syscalls[j].Name) < 0
			})
			actual = append(actual, fam)
		}
	}

	expected := []SyscallFamily{
		{
			decl: decl{
				Name:     fidlgen.MustReadName("example/Foo"),
				Comments: []string{" This gives the foo syscalls."},
			},
			Syscalls: []Syscall{
				{
					member: member{
						Name:     "FooBar",
						Comments: []string{" This is the foo_bar syscall."},
					},
				},
			},
		},
		{
			decl: decl{
				Name:     fidlgen.MustReadName("example/Arbitrary"),
				Comments: []string{" This", " gives", " the", " fizz syscalls."},
			},
			Syscalls: []Syscall{
				{
					member: member{
						Name:     "FizzBuzz",
						Comments: []string{" This", " is", " the", " fizz_buzz syscall."},
					},
				},
			},
		},
		{
			decl: decl{Name: fidlgen.MustReadName("example/Category")},
			Syscalls: []Syscall{
				{
					member:   member{Name: "CategoryBlocking"},
					Blocking: true,
				},
				{
					member:   member{Name: "CategoryConstVdsoCall"},
					Category: SyscallCategoryVdsoCall,
					Const:    true,
				},
				{
					member:   member{Name: "CategoryInternal"},
					Category: SyscallCategoryInternal,
				},
				{
					member:   member{Name: "CategoryNext"},
					Category: SyscallCategoryNext,
				},
				{
					member:   member{Name: "CategoryNoReturn"},
					NoReturn: true,
				},
				{
					member:   member{Name: "CategoryTest0"},
					Testonly: true,
				},
				{
					member:   member{Name: "CategoryTest1"},
					Category: SyscallCategoryTest1,
					Testonly: true,
				},
				{
					member:   member{Name: "CategoryTest2"},
					Category: SyscallCategoryTest2,
					Testonly: true,
				},
				{
					member:   member{Name: "CategoryVdsoCall"},
					Category: SyscallCategoryVdsoCall,
				},
			},
		},
	}

	if diff := cmp.Diff(expected, actual, cmpOpt); diff != "" {
		t.Error(diff)
	}
}
