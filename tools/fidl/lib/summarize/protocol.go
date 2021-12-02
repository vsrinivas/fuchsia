// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package summarize

import (
	"fmt"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

const protocolKind Kind = "protocol"

// addProtocols adds the protocols to the elements list.
func (s *summarizer) addProtocols(protocols []fidlgen.Protocol) {
	for _, p := range protocols {
		for _, m := range p.Methods {
			s.addElement(newMethod(&s.symbols, p.Name, m))
		}
		s.addElement(protocol{named: named{name: Name(p.Name)}})
	}
}

// registerProtocolNames registers the names of all protocols in the FIDL IR.
func (s *summarizer) registerProtocolNames(protocols []fidlgen.Protocol) {
	for _, p := range protocols {
		// This will become useful when deliberating channel syntax.
		s.symbols.addProtocol(p.Name)
	}
}

// protocol represents an element of the protocol type.
type protocol struct {
	named
	notMember
}

// String implements Element.
func (p protocol) String() string {
	return p.Serialize().String()
}

func (p protocol) Serialize() ElementStr {
	e := p.named.Serialize()
	e.Kind = protocolKind
	return e
}

// method represents an Element for a protocol method.
type method struct {
	membership      isMember
	method          fidlgen.Method
	requestPayload  *fidlgen.Struct
	responsePayload *fidlgen.Struct
}

// newMethod creates a new protocol method element.
func newMethod(s *symbolTable, parent fidlgen.EncodedCompoundIdentifier, m fidlgen.Method) method {
	out := method{
		membership: newIsMember(s, parent, m.Name, fidlgen.ProtocolDeclType /* default value */, nil),
		method:     m,
	}
	if m.RequestPayload != nil {
		out.requestPayload = s.getStruct(m.RequestPayload.Identifier)
	}
	if m.ResponsePayload != nil {
		out.responsePayload = s.getStruct(m.ResponsePayload.Identifier)
	}
	return out
}

// Name implements Element.
func (m method) Name() Name {
	return m.membership.Name()
}

// String implements Element.  It formats a protocol method using a notation
// familiar from FIDL.
func (m method) String() string {
	e := m.Serialize()
	// Method serialization is custom because of different spacing.
	return fmt.Sprintf("%v %v%v", e.Kind, e.Name, e.Decl)
}

// Member implements Element.
func (m method) Member() bool {
	return m.membership.Member()
}

// getTypeSignature returns a string representation of the type signature of
// this method.  E.g. "(int32 a) -> (Foo b)"
func (m method) getTypeSignature() Decl {
	var parlist []string
	request := m.getParamList(m.method.HasRequest, m.requestPayload)
	if request != "" {
		parlist = append(parlist, request)
	}
	response := m.getParamList(m.method.HasResponse, m.responsePayload)
	if response != "" {
		if request == "" {
			// -> Method(T a)
			parlist = append(parlist, "")
		}
		parlist = append(parlist, "->", response)
	}
	return Decl(strings.Join(parlist, " "))
}

func (m method) Serialize() ElementStr {
	e := m.membership.Serialize()
	e.Kind = "protocol/member"
	e.Decl = m.getTypeSignature()
	return e
}

// getParamList formats a parameter list, as in Foo(ty1 a, ty2b)
func (m method) getParamList(hasParams bool, payload *fidlgen.Struct) string {
	if !hasParams {
		return ""
	}
	if payload == nil {
		payload = &fidlgen.Struct{}
	}

	params := payload.Members
	var ps []string
	for _, p := range params {
		ps = append(ps, fmt.Sprintf("%v %v", m.membership.symbolTable.fidlTypeString(p.Type), p.Name))
	}
	return fmt.Sprintf("(%v)", strings.Join(ps, ","))
}
