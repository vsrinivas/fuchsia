// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// summarize is a library used to produce a FIDL API summary from the FIDL
// intermediate representation (IR) abstract syntax tree.  Please refer to the
// README.md file in this repository for usage hints.
package summarize

import (
	"encoding/json"
	"fmt"
	"io"
	"sort"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

// Element describes a single platform surface element.  Use Summarize to
// convert a FIDL AST into Elements.
type Element interface {
	// Stringer produces a string representation of this Element.
	fmt.Stringer
	// Member returns true if the Element is a member of something.
	Member() bool
	// Name returns the fully-qualified name of this Element.  For example,
	// "library/protocol.Method".
	Name() Name
	// Serialize converts an Element into a serializable representation.
	Serialize() ElementStr
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
	symbols  symbolTable
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
func (s *summarizer) addUnions(unions []fidlgen.Union) {
	for _, st := range unions {
		for _, m := range st.Members {
			if m.Reserved {
				// Disregard reserved members.
				continue
			}
			s.addElement(newMember(
				&s.symbols, st.Name, m.Name, m.Type, fidlgen.UnionDeclType, nil))
		}
		s.addElement(
			newAggregateWithStrictness(
				st.Name, st.Resourceness, fidlgen.UnionDeclType, st.Strictness))
	}
}

// addTables adds the elements corresponding to the FIDL tables.
func (s *summarizer) addTables(tables []fidlgen.Table) {
	for _, st := range tables {
		for _, m := range st.Members {
			if m.Reserved {
				// Disregard reserved members
				continue
			}
			s.addElement(newMember(&s.symbols, st.Name, m.Name, m.Type, fidlgen.TableDeclType, nil))
		}
		s.addElement(newAggregate(st.Name, st.Resourceness, fidlgen.TableDeclType))
	}
}

// addStructs adds the elements corresponding to the FIDL structs.
func (s *summarizer) addStructs(structs []fidlgen.Struct) {
	for _, st := range structs {
		for _, m := range st.Members {
			s.addElement(newMember(
				&s.symbols, st.Name, m.Name, m.Type, fidlgen.StructDeclType, m.MaybeDefaultValue))
		}
		s.addElement(newAggregate(st.Name, st.Resourceness, fidlgen.StructDeclType))
	}
}

// registerStructs registers names of all the structs in the FIDL IR.
func (s *summarizer) registerStructs(structs []fidlgen.Struct) {
	for _, st := range structs {
		st := st
		s.symbols.addStruct(st.Name, &st)
	}
}

// Write produces an API summary for the FIDL AST from the root into the supplied
// writer.
func Write(root fidlgen.Root, out io.Writer) error {
	for _, e := range Elements(root) {
		fmt.Fprintf(out, "%v\n", e)
	}
	return nil
}

// WriteJSON produces an API summary for the FIDL AST from the root into the
// supplied writer, and formats the data as JSON.
func WriteJSON(root fidlgen.Root, out io.Writer) error {
	e := json.NewEncoder(out)
	// 4-level indent is chosen to match `fx format-code`.
	e.SetIndent("", "    ")
	e.SetEscapeHTML(false)
	return e.Encode(serialize(Elements(root)))
}

func serialize(e []Element) []ElementStr {
	var ret []ElementStr
	for _, l := range e {
		ret = append(ret, l.Serialize())
	}
	return ret
}

// filterStructs takes the list of structs, and excludes all structs that are used as anonymous
// transactional message bodies, as those are explicitly disregarded by the summarizer.
func filterStructs(structs []fidlgen.Struct, root fidlgen.Root) []fidlgen.Struct {
	out := make([]fidlgen.Struct, 0)

	// Do a first pass of the protocols, creating a set of all names of types that are used as a
	// transactional message bodies.
	mbtn := root.GetMessageBodyTypeNames()

	for _, s := range structs {
		if _, ok := mbtn[s.Name]; !ok || !s.IsAnonymous() {
			out = append(out, s)
		}
	}
	return out
}

// Elements returns the API elements found in the supplied AST root in a
// canonical ordering.
func Elements(root fidlgen.Root) []Element {
	var s summarizer

	s.registerStructs(root.Structs)
	s.registerStructs(root.ExternalStructs)
	s.registerProtocolNames(root.Protocols)

	s.addConsts(root.Consts)
	s.addBits(root.Bits)
	s.addEnums(root.Enums)
	s.addStructs(filterStructs(root.Structs, root))
	s.addTables(root.Tables)
	s.addUnions(root.Unions)
	s.addProtocols(root.Protocols)
	s.addElement(library{r: root})
	return s.Elements()
}
