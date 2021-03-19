// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"fmt"
	"strings"

	fidl "go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

//
// Generate code for sending and receiving FIDL messages i.e. the messaging API.
//

// hlMessagingDetails represents the various generated definitions associated
// with a protocol, in the high-level C++ bindings.
type hlMessagingDetails struct {
	// ProtocolMarker is a pure-virtual interface corresponding to methods in
	// the protocol. Notably, HLCPP shares the same interface type between
	// the server and client bindings API.
	ProtocolMarker DeclVariant

	// InterfaceAliasForStub is the type alias generated within the
	// "Stub" class, that refers to the pure-virtual interface corresponding to
	// the protocol.
	InterfaceAliasForStub DeclVariant

	// Proxy implements the interface by encoding and making method calls.
	Proxy DeclVariant

	// Stub calls into the interface after decoding an incoming message.
	// It also implements the EventSender interface.
	Stub DeclVariant

	// EventSender is a pure-virtual interface for sending events.
	EventSender DeclVariant

	// SyncInterface is a pure-virtual interface for making synchronous calls.
	SyncInterface DeclVariant

	// SyncProxy implements the SyncInterface.
	SyncProxy DeclVariant

	RequestEncoder  DeclVariant
	RequestDecoder  DeclVariant
	ResponseEncoder DeclVariant
	ResponseDecoder DeclVariant
}

func compileHlMessagingDetails(protocol DeclName) hlMessagingDetails {
	p := protocol.Natural
	stub := p.AppendName("_Stub")
	return hlMessagingDetails{
		ProtocolMarker:        p,
		InterfaceAliasForStub: stub.Nest(p.AppendName("_clazz").Name()),
		Proxy:                 p.AppendName("_Proxy"),
		Stub:                  stub,
		EventSender:           p.AppendName("_EventSender"),
		SyncInterface:         p.AppendName("_Sync"),
		SyncProxy:             p.AppendName("_SyncProxy"),
		RequestEncoder:        p.AppendName("_RequestEncoder"),
		RequestDecoder:        p.AppendName("_RequestDecoder"),
		ResponseEncoder:       p.AppendName("_ResponseEncoder"),
		ResponseDecoder:       p.AppendName("_ResponseDecoder"),
	}
}

type protocolWithHlMessaging struct {
	*Protocol
	hlMessagingDetails
}

// WithHlMessaging returns a new protocol IR where the HLCPP bindings details
// are promoted to the same naming scope as the protocol. This makes it easier
// to access the HLCPP details in golang templates.
func (p *Protocol) WithHlMessaging() protocolWithHlMessaging {
	return protocolWithHlMessaging{
		Protocol:           p,
		hlMessagingDetails: p.hlMessaging,
	}
}

// TODO(yifeit): Move LLCPP generated code to use this skeleton instead of
// names embedded in golang templates.
type wireMessagingDetails struct {
	ProtocolMarker DeclVariant
	SyncClient     DeclVariant
	ClientImpl     DeclVariant
	EventHandlers  DeclVariant
	Interface      DeclVariant
	EventSender    DeclVariant
}

// TODO(fxbug.dev/60240): Start implementing unified bindings messaging layer
// based on this skeleton.
type unifiedMessagingDetails struct {
	ClientImpl      DeclVariant
	EventHandlers   DeclVariant
	Interface       DeclVariant
	EventSender     DeclVariant
	RequestEncoder  DeclVariant
	RequestDecoder  DeclVariant
	ResponseEncoder DeclVariant
	ResponseDecoder DeclVariant
}

// protocolInner contains information about a Protocol that should be
// filled out by the compiler.
type protocolInner struct {
	fidl.Attributes
	// TODO(yifeit): This should be replaced by ProtocolMarker in hlMessagingDetails
	// and wireMessagingDetails. In particular, the unified bindings do not declare
	// protocol marker classes.
	DeclName

	// [Discoverable] protocols are exported to the outgoing namespace under this
	// name. This is deprecated by FTP-041 unified services.
	// TODO(fxbug.dev/8035): Remove.
	DiscoverableName string

	hlMessaging hlMessagingDetails

	// ClientAllocation is the allocation behavior of the client when receiving
	// FIDL events over this protocol.
	SyncEventAllocation allocation
	Methods             []Method
	FuzzingName         string
	TestBase            DeclName
}

// Protocol should be created using newProtocol.
type Protocol struct {
	protocolInner

	// OneWayMethods contains the list of one-way (i.e. fire-and-forget) methods
	// in the protocol.
	OneWayMethods []Method

	// TwoWayMethods contains the list of two-way (i.e. has both request and
	// response) methods in the protocol.
	TwoWayMethods []Method

	// ClientMethods contains the list of client-initiated methods (i.e. any
	// interaction that is not an event). It is the union of one-way and two-way
	// methods.
	ClientMethods []Method

	// Events contains the list of events (i.e. initiated by servers)
	// in the protocol.
	Events []Method

	// Kind is a type tag; omit when initializing the struct.
	Kind protocolKind
}

func (p *Protocol) Name() string {
	return p.Wire.Name() // TODO: not the wire name, maybe?
}

func (p *Protocol) NaturalType() string {
	return string(p.Natural.Type())
}

func (p *Protocol) WireType() string {
	return string(p.Wire.Type())
}

func newProtocol(inner protocolInner) *Protocol {
	type kinds []methodKind

	filterBy := func(kinds kinds) []Method {
		var out []Method
		for _, m := range inner.Methods {
			k := m.methodKind()
			for _, want := range kinds {
				if want == k {
					out = append(out, m)
				}
			}
		}
		return out
	}

	return &Protocol{
		protocolInner: inner,
		OneWayMethods: filterBy(kinds{oneWayMethod}),
		TwoWayMethods: filterBy(kinds{twoWayMethod}),
		ClientMethods: filterBy(kinds{oneWayMethod, twoWayMethod}),
		Events:        filterBy(kinds{eventMethod}),
	}
}

type argsWrapper []Parameter

// TODO(fxb/7704): We should be able to remove as we align with args with struct
// representation.
func (args argsWrapper) isResource() bool {
	for _, arg := range args {
		if arg.Type.IsResource {
			return true
		}
	}
	return false
}

// messageInner contains information about a Message that should be filled out
// by the compiler.
type messageInner struct {
	fidl.TypeShape
	CodingTable DeclName
}

// message contains lower level wire-format information about a request/response
// message.
// message should be created using newMessage.
type message struct {
	messageInner

	fidl.Strictness
	IsResource       bool
	ClientAllocation allocation
	ServerAllocation allocation
}

// methodContext indicates where the request/response is used.
// The allocation strategies differ for client and server contexts, in LLCPP.
type methodContext int

const (
	_ methodContext = iota
	clientContext
	serverContext
)

type boundednessQuery func(methodContext, fidl.Strictness) boundedness

func newMessage(inner messageInner, args []Parameter, boundednessQuery boundednessQuery) message {
	strictness := fidl.Strictness(!inner.TypeShape.HasFlexibleEnvelope)
	return message{
		messageInner: inner,
		Strictness:   strictness,
		IsResource:   argsWrapper(args).isResource(),
		ClientAllocation: computeAllocation(
			inner.InlineSize,
			inner.MaxOutOfLine,
			boundednessQuery(clientContext, strictness)),
		ServerAllocation: computeAllocation(
			inner.InlineSize,
			inner.MaxOutOfLine,
			boundednessQuery(serverContext, strictness)),
	}
}

func (m message) HasPointer() bool {
	return m.Depth > 0
}

// methodInner contains information about a Method that should be filled out by
// the compiler.
type methodInner struct {
	protocolName DeclName
	request      messageInner
	response     messageInner

	fidl.Attributes
	Name         string
	Ordinal      uint64
	HasRequest   bool
	RequestArgs  []Parameter
	HasResponse  bool
	ResponseArgs []Parameter
	Transitional bool
	Result       *Result
}

// Method should be created using newMethod.
type Method struct {
	methodInner
	NameInLowerSnakeCase string
	OrdinalName          string
	Request              message
	Response             message
	CallbackType         string
	ResponseHandlerType  string
	ResponderType        string
	// Protocol is a reference to the containing protocol, for the
	// convenience of golang templates.
	Protocol DeclName
}

type messageDirection int

const (
	_ messageDirection = iota
	messageDirectionRequest
	messageDirectionResponse
)

// Compute boundedness based on client/server, request/response, and strictness.
func (d messageDirection) queryBoundedness(c methodContext, s fidl.Strictness) boundedness {
	switch d {
	case messageDirectionRequest:
		if c == clientContext {
			// Allocation is bounded when sending request from a client.
			return boundednessBounded
		} else {
			return boundedness(s.IsStrict())
		}
	case messageDirectionResponse:
		if c == serverContext {
			// Allocation is bounded when sending response from a server.
			return boundednessBounded
		} else {
			return boundedness(s.IsStrict())
		}
	}
	panic(fmt.Sprintf("unexpected message direction: %v", d))
}

func newMethod(inner methodInner) Method {
	callbackType := ""
	if inner.HasResponse {
		callbackType = changeIfReserved(fidl.Identifier(inner.Name + "Callback"))
	}

	m := Method{
		methodInner:          inner,
		NameInLowerSnakeCase: fidl.ToSnakeCase(inner.Name),
		OrdinalName:          fmt.Sprintf("k%s_%s_Ordinal", inner.protocolName.Natural.Name(), inner.Name),
		Request: newMessage(inner.request,
			inner.RequestArgs, messageDirectionRequest.queryBoundedness),
		Response: newMessage(inner.response,
			inner.ResponseArgs, messageDirectionResponse.queryBoundedness),
		CallbackType:        callbackType,
		ResponseHandlerType: fmt.Sprintf("%s_%s_ResponseHandler", inner.protocolName.Natural.Name(), inner.Name),
		ResponderType:       fmt.Sprintf("%s_%s_Responder", inner.protocolName.Natural.Name(), inner.Name),
		Protocol:            inner.protocolName,
	}
	return m
}

type methodKind int

const (
	oneWayMethod = methodKind(iota)
	twoWayMethod
	eventMethod
)

func (m *Method) methodKind() methodKind {
	if m.HasRequest {
		if m.HasResponse {
			return twoWayMethod
		}
		return oneWayMethod
	}
	if !m.HasResponse {
		panic("A method should have at least either a request or a response")
	}
	return eventMethod
}

func (m *Method) CallbackWrapper() string {
	return "fit::function"
}

type Parameter struct {
	Type              Type
	Name              string
	Offset            int
	HandleInformation *HandleInformation
}

func (p Parameter) NameAndType() (string, Type) {
	return p.Name, p.Type
}

func allEventsStrict(methods []Method) fidl.Strictness {
	strictness := fidl.IsStrict
	for _, m := range methods {
		if !m.HasRequest && m.HasResponse && m.Response.IsFlexible() {
			strictness = fidl.IsFlexible
			break
		}
	}
	return strictness
}

func (c *compiler) compileProtocol(val fidl.Protocol) *Protocol {
	protocolName := c.compileDeclName(val.Name)
	codingTableName := codingTableName(val.Name)
	codingTableBase := DeclName{
		Wire:    NewDeclVariant(codingTableName, protocolName.Wire.Namespace()),
		Natural: NewDeclVariant(codingTableName, protocolName.Natural.Namespace().Append("_internal")),
	}
	methods := []Method{}
	for _, v := range val.Methods {
		name := changeIfReserved(v.Name)
		requestCodingTable := codingTableBase.AppendName(string(v.Name) + "RequestTable")
		responseCodingTable := codingTableBase.AppendName(string(v.Name) + "ResponseTable")
		if !v.HasRequest {
			responseCodingTable = codingTableBase.AppendName(string(v.Name) + "EventTable")
		}

		var result *Result
		if v.HasResponse && len(v.Response) == 1 {
			// If the method uses the error syntax, Response[0] will be a union
			// that was placed in c.resultForUnion. Otherwise, this will be nil.
			result = c.resultForUnion[v.Response[0].Type.Identifier]
		}

		method := newMethod(methodInner{
			protocolName: protocolName,
			request: messageInner{
				TypeShape:   v.RequestTypeShapeV1,
				CodingTable: requestCodingTable,
			},
			response: messageInner{
				TypeShape:   v.ResponseTypeShapeV1,
				CodingTable: responseCodingTable,
			},
			Attributes:   v.Attributes,
			Name:         name,
			Ordinal:      v.Ordinal,
			HasRequest:   v.HasRequest,
			RequestArgs:  c.compileParameterArray(v.Request),
			HasResponse:  v.HasResponse,
			ResponseArgs: c.compileParameterArray(v.Response),
			Transitional: v.IsTransitional(),
			Result:       result,
		})
		methods = append(methods, method)
	}

	var maxResponseSize int
	for _, method := range methods {
		if size := method.Response.InlineSize + method.Response.MaxOutOfLine; size > maxResponseSize {
			maxResponseSize = size
		}
	}

	fuzzingName := strings.ReplaceAll(strings.ReplaceAll(string(val.Name), ".", "_"), "/", "_")
	r := newProtocol(protocolInner{
		Attributes:       val.Attributes,
		DeclName:         protocolName,
		hlMessaging:      compileHlMessagingDetails(protocolName),
		DiscoverableName: val.GetServiceName(),
		SyncEventAllocation: computeAllocation(
			maxResponseSize, 0, messageDirectionResponse.queryBoundedness(
				clientContext, allEventsStrict(methods))),
		Methods:     methods,
		FuzzingName: fuzzingName,
		TestBase:    protocolName.AppendName("_TestBase").AppendNamespace("testing"),
	})
	return r
}

func (c *compiler) compileParameterArray(val []fidl.Parameter) []Parameter {
	var params []Parameter = []Parameter{}
	for _, v := range val {
		params = append(params, Parameter{
			Type:              c.compileType(v.Type),
			Name:              changeIfReserved(v.Name),
			Offset:            v.FieldShapeV1.Offset,
			HandleInformation: c.fieldHandleInformation(&v.Type),
		})
	}
	return params
}

//
// Functions for calculating message buffer size bounds
//

func fidlAlign(size int) int {
	return (size + 7) & ^7
}

type boundedness bool

const (
	boundednessBounded   = true
	boundednessUnbounded = false
)

// This value needs to be kept in sync with the one defined in
// zircon/system/ulib/fidl/include/lib/fidl/llcpp/sync_call.h
const llcppMaxStackAllocSize = 512
const channelMaxMessageSize = 65536

// allocation describes the allocation strategy of some operation, such as
// sending requests, receiving responses, or handling events. Note that the
// allocation strategy may depend on client/server context, direction of the
// message, and the content/shape of the message, as we make optimizations.
type allocation struct {
	IsStack bool
	Size    int

	bufferType bufferType
	size       string
}

func (alloc allocation) ByteBufferType() string {
	switch alloc.bufferType {
	case inlineBuffer:
		return fmt.Sprintf("::fidl::internal::InlineMessageBuffer<%s>", alloc.size)
	case boxedBuffer:
		return fmt.Sprintf("::fidl::internal::BoxedMessageBuffer<%s>", alloc.size)
	}
	panic(fmt.Sprintf("unexpected buffer type: %v", alloc.bufferType))
}

type bufferType int

const (
	_ bufferType = iota
	inlineBuffer
	boxedBuffer
)

func computeAllocation(primarySize int, maxOutOfLine int, boundedness boundedness) allocation {
	var sizeString string
	var size int
	if boundedness == boundednessUnbounded || primarySize+maxOutOfLine > channelMaxMessageSize {
		sizeString = "ZX_CHANNEL_MAX_MSG_BYTES"
		size = channelMaxMessageSize
	} else {
		size = fidlAlign(primarySize + maxOutOfLine)
		sizeString = fmt.Sprintf("%d", size)
	}

	if size > llcppMaxStackAllocSize {
		return allocation{
			IsStack:    false,
			Size:       0,
			bufferType: boxedBuffer,
			size:       sizeString,
		}
	} else {
		return allocation{
			IsStack:    true,
			Size:       size,
			bufferType: inlineBuffer,
			size:       sizeString,
		}
	}
}
