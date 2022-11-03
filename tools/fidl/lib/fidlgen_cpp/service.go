// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"fmt"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

//
// Code generation for unified services.
// See https://fuchsia.dev/fuchsia-src/contribute/governance/fidl/ftp/ftp-041.
//

type Service struct {
	Attributes
	nameVariants
	ServiceName string
	Transport   Transport
	Members     []ServiceMember
}

func (*Service) Kind() declKind {
	return Kinds.Service
}

var _ Kinded = (*Service)(nil)
var _ namespaced = (*Service)(nil)

type ServiceMember struct {
	Attributes
	nameVariants
	ClassName         nameVariants
	Service           *Service
	ProtocolType      nameVariants
	ProtocolTransport Transport
}

func (c *compiler) compileService(val fidlgen.Service) *Service {
	t := "Channel"
	if len(val.Members) > 0 {
		t = val.Members[0].Type.ProtocolTransport
	}

	transport, ok := transports[t]
	if !ok {
		panic(fmt.Sprintf("transport %s not found", t))
	}
	s := Service{
		Attributes:   Attributes{val.Attributes},
		nameVariants: c.compileNameVariants(val.Name),
		Transport:    *transport,
		ServiceName:  val.GetServiceName(),
	}

	for _, v := range val.Members {
		s.Members = append(s.Members, c.compileServiceMember(v, &s))
	}
	return &s
}

func (c *compiler) compileServiceMember(val fidlgen.ServiceMember, s *Service) ServiceMember {
	return ServiceMember{
		Attributes:        Attributes{val.Attributes},
		nameVariants:      serviceMemberContext.transform(val.Name),
		ClassName:         serviceMemberTypeContext.transform(val.Name),
		Service:           s,
		ProtocolType:      c.compileNameVariants(val.Type.Identifier),
		ProtocolTransport: *transports[val.Type.ProtocolTransport],
	}
}

// createType formats type names for this protocol in the new C++ bindings.
func (sm ServiceMember) createType(kind string) string {
	return fmt.Sprintf("::%s::%s<%s>", sm.ProtocolTransport.Namespace, kind, sm.ProtocolType)
}

// ServerEnd returns the type for server ends of this protocol in the new C++ bindings.
func (sm ServiceMember) ServerEnd() string {
	return sm.createType("ServerEnd")
}

// ClientEnd returns the type for client ends of this protocol in the new C++ bindings.
func (sm ServiceMember) ClientEnd() string {
	return sm.createType("ClientEnd")
}

// UnownedClientEnd returns the type for unowned client ends of this protocol in the new C++ bindings.
func (sm ServiceMember) CreateEndpoints() string {
	return sm.createType("CreateEndpoints")
}
