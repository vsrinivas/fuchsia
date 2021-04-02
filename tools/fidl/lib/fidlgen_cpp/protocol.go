// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"fmt"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

//
// Generate code for sending and receiving FIDL messages i.e. the messaging API.
//

// hlMessagingDetails represents the various generated definitions associated
// with a protocol, in the high-level C++ bindings.
// TODO(fxbug.dev/72798): Use the same approach to pass wireTypeNames and
// hlMessagingDetails to the templates.
type hlMessagingDetails struct {
	// ProtocolMarker is a pure-virtual interface corresponding to methods in
	// the protocol. Notably, HLCPP shares the same interface type between
	// the server and client bindings API.
	ProtocolMarker Name

	// InterfaceAliasForStub is the type alias generated within the
	// "Stub" class, that refers to the pure-virtual interface corresponding to
	// the protocol.
	InterfaceAliasForStub Name

	// Proxy implements the interface by encoding and making method calls.
	Proxy Name

	// Stub calls into the interface after decoding an incoming message.
	// It also implements the EventSender interface.
	Stub Name

	// EventSender is a pure-virtual interface for sending events.
	EventSender Name

	// SyncInterface is a pure-virtual interface for making synchronous calls.
	SyncInterface Name

	// SyncProxy implements the SyncInterface.
	SyncProxy Name

	RequestEncoder  Name
	RequestDecoder  Name
	ResponseEncoder Name
	ResponseDecoder Name
}

func compileHlMessagingDetails(protocol NameVariants) hlMessagingDetails {
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
	Protocol
	hlMessagingDetails
}

// WithHlMessaging returns a new protocol IR where the HLCPP bindings details
// are promoted to the same naming scope as the protocol. This makes it easier
// to access the HLCPP details in golang templates.
func (p Protocol) WithHlMessaging() protocolWithHlMessaging {
	return protocolWithHlMessaging{
		Protocol:           p,
		hlMessagingDetails: p.hlMessaging,
	}
}

// TODO(fxbug.dev/60240): Start implementing unified bindings messaging layer
// based on this skeleton.
type unifiedMessagingDetails struct {
	ClientImpl      Name
	EventHandlers   Name
	Interface       Name
	EventSender     Name
	RequestEncoder  Name
	RequestDecoder  Name
	ResponseEncoder Name
	ResponseDecoder Name
}

// These correspond to templated classes forward-declared in
// /zircon/system/ulib/fidl/include/lib/fidl/llcpp/wire_messaging.h
var (
	// Protocol related
	WireSyncClient            = fidlNs.Member("WireSyncClient")
	WireClient                = fidlNs.Member("WireClient")
	WireEventHandlerInterface = internalNs.Member("WireEventHandlerInterface")
	WireSyncEventHandler      = fidlNs.Member("WireSyncEventHandler")
	WireAsyncEventHandler     = fidlNs.Member("WireAsyncEventHandler")
	WireInterface             = fidlNs.Member("WireInterface")
	WireRawChannelInterface   = fidlNs.Member("WireRawChannelInterface")
	WireEventSender           = fidlNs.Member("WireEventSender")
	WireWeakEventSender       = internalNs.Member("WireWeakEventSender")
	WireClientImpl            = internalNs.Member("WireClientImpl")
	WireCaller                = internalNs.Member("WireCaller")
	WireDispatcher            = internalNs.Member("WireDispatcher")

	// MethodRelated
	WireRequest         = fidlNs.Member("WireRequest")
	WireResponse        = fidlNs.Member("WireResponse")
	WireResult          = fidlNs.Member("WireResult")
	WireUnownedResult   = fidlNs.Member("WireUnownedResult")
	WireResponseContext = fidlNs.Member("WireResponseContext")
)

type wireTypeNames struct {
	// WireProtocolMarker is a class only used for containing other definitions
	// related to this protocol.
	// TODO(fxbug.dev/72798): Golang template should use this instead of the
	// NameVariants embedded in Protocol.
	WireProtocolMarker        Name
	WireSyncClient            Name
	WireClient                Name
	WireEventHandlerInterface Name
	WireSyncEventHandler      Name
	WireAsyncEventHandler     Name
	WireInterface             Name
	WireRawChannelInterface   Name
	WireEventSender           Name
	WireWeakEventSender       Name
	WireClientImpl            Name
	WireCaller                Name
	WireDispatcher            Name
}

func newWireTypeNames(protocolVariants NameVariants) wireTypeNames {
	p := protocolVariants.Wire
	return wireTypeNames{
		WireProtocolMarker:        p,
		WireSyncClient:            WireSyncClient.Template(p),
		WireClient:                WireClient.Template(p),
		WireEventHandlerInterface: WireEventHandlerInterface.Template(p),
		WireSyncEventHandler:      WireSyncEventHandler.Template(p),
		WireAsyncEventHandler:     WireAsyncEventHandler.Template(p),
		WireInterface:             WireInterface.Template(p),
		WireRawChannelInterface:   WireRawChannelInterface.Template(p),
		WireEventSender:           WireEventSender.Template(p),
		WireWeakEventSender:       WireWeakEventSender.Template(p),
		WireClientImpl:            WireClientImpl.Template(p),
		WireCaller:                WireCaller.Template(p),
		WireDispatcher:            WireDispatcher.Template(p),
	}
}

// protocolInner contains information about a Protocol that should be
// filled out by the compiler.
type protocolInner struct {
	Attributes
	// TODO(fxbug.dev/72798): This should be replaced by ProtocolMarker in hlMessagingDetails
	// and wireMessagingDetails. In particular, the unified bindings do not declare
	// protocol marker classes.
	NameVariants

	// [Discoverable] protocols are exported to the outgoing namespace under this
	// name. This is deprecated by FTP-041 unified services.
	// TODO(fxbug.dev/8035): Remove.
	DiscoverableName string

	hlMessaging hlMessagingDetails
	wireTypeNames

	// ClientAllocation is the allocation behavior of the client when receiving
	// FIDL events over this protocol.
	SyncEventAllocation allocation
	Methods             []Method
	FuzzingName         string
	TestBase            NameVariants
}

// Protocol should be created using newProtocol.
type Protocol struct {
	protocolInner

	// OneWayMethods contains the list of one-way (i.e. fire-and-forget) methods
	// in the protocol.
	OneWayMethods []*Method

	// TwoWayMethods contains the list of two-way (i.e. has both request and
	// response) methods in the protocol.
	TwoWayMethods []*Method

	// ClientMethods contains the list of client-initiated methods (i.e. any
	// interaction that is not an event). It is the union of one-way and two-way
	// methods.
	ClientMethods []*Method

	// Events contains the list of events (i.e. initiated by servers)
	// in the protocol.
	Events []*Method

	// Templated names for wire helper types.
}

func (Protocol) Kind() declKind {
	return Kinds.Protocol
}

var _ Kinded = (*Protocol)(nil)

func (p Protocol) Name() string {
	return p.Wire.Name() // TODO: not the wire name, maybe?
}

func (p Protocol) NaturalType() string {
	return p.Natural.String()
}

func (p Protocol) WireType() string {
	return p.Wire.String()
}

func newProtocol(inner protocolInner) Protocol {
	type kinds []methodKind

	filterBy := func(kinds kinds) []*Method {
		var out []*Method
		for i := 0; i < len(inner.Methods); i++ {
			m := &inner.Methods[i]
			k := m.methodKind()
			for _, want := range kinds {
				if want == k {
					out = append(out, m)
				}
			}
		}
		return out
	}

	return Protocol{
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

type messageInner struct {
	fidlgen.TypeShape
	HlCodingTable   Name
	WireCodingTable Name
}

// message contains lower level wire-format information about a request/response
// message.
// message should be created using newMessage.
type message struct {
	messageInner
	fidlgen.Strictness
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

type boundednessQuery func(methodContext, fidlgen.Strictness) boundedness

func newMessage(inner messageInner, args []Parameter, wire wireTypeNames,
	direction messageDirection) message {
	ts := inner.TypeShape
	strictness := fidlgen.Strictness(!ts.HasFlexibleEnvelope)
	return message{
		messageInner: inner,
		Strictness:   strictness,
		IsResource:   argsWrapper(args).isResource(),
		ClientAllocation: computeAllocation(
			ts.InlineSize,
			ts.MaxOutOfLine,
			direction.queryBoundedness(clientContext, strictness)),
		ServerAllocation: computeAllocation(
			ts.InlineSize,
			ts.MaxOutOfLine,
			direction.queryBoundedness(serverContext, strictness)),
	}
}

func (m message) HasPointer() bool {
	return m.Depth > 0
}

type wireMethod struct {
	WireCompleter       Name
	WireCompleterBase   Name
	WireRequest         Name
	WireRequestAlias    Name
	WireResponse        Name
	WireResponseAlias   Name
	WireResponseContext Name
	WireResult          Name
	WireUnownedResult   Name
}

func newWireMethod(name string, wireTypes wireTypeNames, protocolMarker Name, methodMarker Name) wireMethod {
	m := protocolMarker.Nest(name)
	i := wireTypes.WireInterface.Nest(name)
	return wireMethod{
		WireCompleter:       i.AppendName("Completer"),
		WireCompleterBase:   i.AppendName("CompleterBase"),
		WireRequest:         WireRequest.Template(methodMarker),
		WireRequestAlias:    m.AppendName("Request"),
		WireResponse:        WireResponse.Template(methodMarker),
		WireResponseAlias:   m.AppendName("Response"),
		WireResponseContext: WireResponseContext.Template(methodMarker),
		WireResult:          WireResult.Template(methodMarker),
		WireUnownedResult:   WireUnownedResult.Template(methodMarker),
	}
}

// methodInner contains information about a Method that should be filled out by
// the compiler.
type methodInner struct {
	protocolName NameVariants
	Marker       NameVariants
	wireMethod
	baseCodingTableName string
	requestTypeShape    fidlgen.TypeShape
	responseTypeShape   fidlgen.TypeShape

	Attributes
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
	OrdinalName          NameVariants
	Request              message
	Response             message
	CallbackType         string
	ResponseHandlerType  string
	ResponderType        string
	// Protocol is a reference to the containing protocol, for the
	// convenience of golang templates.
	Protocol *Protocol
}

type messageDirection int

const (
	_ messageDirection = iota
	messageDirectionRequest
	messageDirectionResponse
)

// Compute boundedness based on client/server, request/response, and strictness.
func (d messageDirection) queryBoundedness(c methodContext, s fidlgen.Strictness) boundedness {
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

func newMethod(inner methodInner, hl hlMessagingDetails, wire wireTypeNames) Method {
	hlCodingTableBase := hl.ProtocolMarker.Namespace().Append("_internal").Member(inner.baseCodingTableName)
	wireCodingTableBase := wire.WireProtocolMarker.Namespace().Member(inner.baseCodingTableName)

	hlRequestCodingTable := hlCodingTableBase.AppendName("RequestTable")
	wireRequestCodingTable := wireCodingTableBase.AppendName("RequestTable")
	hlResponseCodingTable := hlCodingTableBase.AppendName("ResponseTable")
	wireResponseCodingTable := wireCodingTableBase.AppendName("ResponseTable")
	if !inner.HasRequest {
		hlResponseCodingTable = hlCodingTableBase.AppendName("EventTable")
		wireResponseCodingTable = wireCodingTableBase.AppendName("EventTable")
	}

	callbackType := ""
	if inner.HasResponse {
		callbackType = changeIfReserved(fidlgen.Identifier(inner.Name + "Callback"))
	}
	ordinalName := fmt.Sprintf("k%s_%s_Ordinal", inner.protocolName.Natural.Name(), inner.Name)

	m := Method{
		methodInner:          inner,
		NameInLowerSnakeCase: fidlgen.ToSnakeCase(inner.Name),
		OrdinalName: NameVariants{
			Natural: inner.protocolName.Natural.Namespace().Append("internal").Member(ordinalName),
			Wire:    inner.protocolName.Wire.Namespace().Member(ordinalName),
		},
		Request: newMessage(messageInner{
			TypeShape:       inner.requestTypeShape,
			HlCodingTable:   hlRequestCodingTable,
			WireCodingTable: wireRequestCodingTable,
		}, inner.RequestArgs, wire, messageDirectionRequest),
		Response: newMessage(messageInner{
			TypeShape:       inner.responseTypeShape,
			HlCodingTable:   hlResponseCodingTable,
			WireCodingTable: wireResponseCodingTable,
		}, inner.ResponseArgs, wire, messageDirectionResponse),
		CallbackType:        callbackType,
		ResponseHandlerType: fmt.Sprintf("%s_%s_ResponseHandler", inner.protocolName.Natural.Name(), inner.Name),
		ResponderType:       fmt.Sprintf("%s_%s_Responder", inner.protocolName.Natural.Name(), inner.Name),
		Protocol:            nil,
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

func allEventsStrict(methods []Method) fidlgen.Strictness {
	strictness := fidlgen.IsStrict
	for _, m := range methods {
		if !m.HasRequest && m.HasResponse && m.Response.IsFlexible() {
			strictness = fidlgen.IsFlexible
			break
		}
	}
	return strictness
}

func (c *compiler) compileProtocol(p fidlgen.Protocol) Protocol {
	protocolName := c.compileNameVariants(p.Name)
	codingTableName := codingTableName(p.Name)
	hlMessaging := compileHlMessagingDetails(protocolName)
	wireTypeNames := newWireTypeNames(protocolName)

	methods := []Method{}
	for _, v := range p.Methods {
		name := changeIfReserved(v.Name)

		var result *Result
		if v.MethodResult != nil {
			// If the method uses the error syntax, Response[0] will be a union
			// that was placed in c.resultForUnion. Otherwise, this will be nil.
			result = c.resultForUnion[v.Response[0].Type.Identifier]
		}

		methodMarker := protocolName.Nest(name)

		method := newMethod(methodInner{
			protocolName: protocolName,
			// Using the raw identifier v.Name instead of the name after
			// reserved words logic, since that's the behavior in fidlc.
			baseCodingTableName: codingTableName + string(v.Name),
			Marker:              methodMarker,
			requestTypeShape:    v.RequestTypeShapeV1,
			responseTypeShape:   v.ResponseTypeShapeV1,
			wireMethod:          newWireMethod(name, wireTypeNames, protocolName.Wire, methodMarker.Wire),
			Attributes:          Attributes{v.Attributes},
			Name:                name,
			Ordinal:             v.Ordinal,
			HasRequest:          v.HasRequest,
			RequestArgs:         c.compileParameterArray(v.Request),
			HasResponse:         v.HasResponse,
			ResponseArgs:        c.compileParameterArray(v.Response),
			Transitional:        v.IsTransitional(),
			Result:              result,
		}, hlMessaging, wireTypeNames)
		methods = append(methods, method)
	}

	var maxResponseSize int
	for _, method := range methods {
		if size := method.Response.InlineSize + method.Response.MaxOutOfLine; size > maxResponseSize {
			maxResponseSize = size
		}
	}

	fuzzingName := strings.ReplaceAll(strings.ReplaceAll(string(p.Name), ".", "_"), "/", "_")
	r := newProtocol(protocolInner{
		Attributes:       Attributes{p.Attributes},
		NameVariants:     protocolName,
		hlMessaging:      hlMessaging,
		wireTypeNames:    wireTypeNames,
		DiscoverableName: p.GetServiceName(),
		SyncEventAllocation: computeAllocation(
			maxResponseSize, 0, messageDirectionResponse.queryBoundedness(
				clientContext, allEventsStrict(methods))),
		Methods:     methods,
		FuzzingName: fuzzingName,
		TestBase:    protocolName.AppendName("_TestBase").AppendNamespace("testing"),
	})
	for i := 0; i < len(methods); i++ {
		methods[i].Protocol = &r
	}
	return r
}

func (c *compiler) compileParameterArray(val []fidlgen.Parameter) []Parameter {
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
