// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package summarize

import (
	"strings"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

// addProtocols adds the protocols to the elements list.
func (s *summarizer) addProtocols(protocols []fidlgen.Protocol) {
	for _, p := range protocols {
		for _, m := range p.Methods {
			s.addElement(newMethod(&s.symbols, p.Name, m))
		}
		s.addElement(&protocol{named: named{name: Name(p.Name)}})
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

func (p *protocol) Serialize() ElementStr {
	e := p.named.Serialize()
	e.Kind = ProtocolKind
	return e
}

// method represents an Element for a protocol method.
type method struct {
	membership      isMember
	method          fidlgen.Method
	requestPayload  parameterizer
	responsePayload parameterizer
}

// newMethod creates a new protocol method element.
func newMethod(s *symbolTable, parent fidlgen.EncodedCompoundIdentifier, m fidlgen.Method) *method {
	out := &method{
		membership: *newIsMember(s, parent, m.Name, fidlgen.ProtocolDeclType /* default value */, nil),
		method:     m,
	}
	if m.RequestPayload != nil {
		out.requestPayload = s.getPayload(m.RequestPayload.Identifier)
	}
	if m.ResponsePayload != nil {
		out.responsePayload = s.getPayload(m.ResponsePayload.Identifier)
	}
	return out
}

// Name implements Element.
func (m *method) Name() Name {
	return m.membership.Name()
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

func (m *method) Serialize() ElementStr {
	e := m.membership.Serialize()
	e.Kind = ProtocolMemberKind
	e.Decl = m.getTypeSignature()
	return e
}

// getParamList formats a parameter list, as in Foo(ty1 a, ty2b)
func (m method) getParamList(hasParams bool, payload parameterizer) string {
	if !hasParams {
		return ""
	}
	if payload == nil {
		payload = &structPayload{}
	}

	typePrinter := m.membership.symbolTable.fidlTypeString
	return payload.AsParameters(typePrinter)
}
