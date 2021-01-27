// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fmt"
	"io"
	"sort"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

// Element describes a single platform surface element.  Use Summarize to
// convert a FIDL AST into Elements.
type Element interface {
	fmt.Stringer
	// Member returns true if the Element is a member of something.
	Member() bool
	// Name returns the fully-qualified name of this Element.  For example,
	// "library/protocol.Method".
	Name() string
}

// All implementers of Element.
var _ = []Element{
	(*bits)(nil),
	(*aConst)(nil),
	(*enum)(nil),
	(*method)(nil),
	(*protocol)(nil),
	(*library)(nil),
}

type summarizer struct {
	elements elementSlice
}

// addElement adds an element for summarization.
func (s *summarizer) addElement(e Element) {
	s.elements = append(s.elements, e)
}

// Elements obtains the API elements in this summarizer.
func (s *summarizer) Elements() []Element {
	// Ensure predictable ordering of the reported Elements.
	sort.Sort(s.elements)
	return s.elements
}

// addUnions adds the elements corresponding to the FIDL unions.
func (s *summarizer) addUnions(structs []fidlgen.Union) {
	for _, st := range structs {
		for _, m := range st.Members {
			s.addElement(newMember(
				st.Name, m.Name, m.Type, fidlgen.UnionDeclType))
		}
		s.addElement(
			newAggregate(st.Name, st.Resourceness, fidlgen.UnionDeclType))
	}
}

// addTables adds the elements corresponding to the FIDL tables.
func (s *summarizer) addTables(structs []fidlgen.Table) {
	for _, st := range structs {
		for _, m := range st.Members {
			s.addElement(newMember(st.Name, m.Name, m.Type, fidlgen.TableDeclType))
		}
		s.addElement(newAggregate(st.Name, st.Resourceness, fidlgen.TableDeclType))
	}
}

// addStructs adds the elements corresponding to the FIDL structs.
func (s *summarizer) addStructs(structs []fidlgen.Struct) {
	for _, st := range structs {
		if st.Anonymous {
			// Disregard anonymous structs for API summarization.
			continue
		}
		for _, m := range st.Members {
			s.addElement(newMember(
				st.Name, m.Name, m.Type, fidlgen.StructDeclType))
		}
		s.addElement(newAggregate(st.Name, st.Resourceness, fidlgen.StructDeclType))
	}
}

/// Summarize produces an API summary for the FIDL AST from the root.
func Summarize(root fidlgen.Root, out io.Writer) error {
	var s summarizer
	s.addConsts(root.Consts)
	s.addBits(root.Bits)
	s.addEnums(root.Enums)
	s.addStructs(root.Structs)
	s.addTables(root.Tables)
	s.addUnions(root.Unions)
	s.addProtocols(root.Protocols)
	s.addElement(library{r: root})
	for _, e := range s.Elements() {
		fmt.Fprintf(out, "%v\n", e)
	}
	return nil
}

func elementCountToString(ec *int) string {
	if ec != nil {
		return fmt.Sprintf(":%d", *ec)
	} else {
		return ""
	}
}

func nullableToString(n bool) string {
	if n {
		return "?"
	} else {
		return ""
	}
}

// fidlNestedToString prints the FIDL type of a sequence or aggregate, assuming
// that t is indeed such a type.
func fidlNestedToString(t fidlgen.Type) string {
	return fmt.Sprintf("%v<%v>%v%v",
		// Assumes t.Kind is one of the sequential types.
		t.Kind,
		fidlTypeString(*t.ElementType),
		elementCountToString(t.ElementCount),
		nullableToString(t.Nullable))
}

// fidlTypeString converts the FIDL type declaration into a string.
func fidlTypeString(t fidlgen.Type) string {
	n := nullableToString(t.Nullable)
	switch t.Kind {
	case fidlgen.PrimitiveType:
		return string(t.PrimitiveSubtype)
	case fidlgen.StringType:
		return fmt.Sprintf("%s%s%s",
			t.Kind, elementCountToString(t.ElementCount), n)
	case fidlgen.ArrayType, fidlgen.VectorType:
		return fidlNestedToString(t)
	case fidlgen.HandleType:
		switch t.HandleSubtype {
		case fidlgen.Handle:
			return fmt.Sprintf("handle%v", n)
		default:
			return fmt.Sprintf(
				"zx/handle:zx/obj_type.%v%v", strings.ToUpper(string(t.HandleSubtype)), n)
		}
	case fidlgen.IdentifierType:
		return fmt.Sprintf("%v%v", string(t.Identifier), n)
	case fidlgen.RequestType:
		return fmt.Sprintf("request<%v>%v", string(t.RequestSubtype), n)
	default:
		return "<not implemented>"
	}
}
