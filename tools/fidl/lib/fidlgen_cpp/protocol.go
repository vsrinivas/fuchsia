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

// protocolInner contains information about a Protocol that should be
// filled out by the compiler.
type protocolInner struct {
	fidl.Attributes
	DeclName
	ClassName           string
	ServiceName         string
	ProxyName           DeclVariant
	StubName            DeclVariant
	EventSenderName     DeclVariant
	SyncName            DeclVariant
	SyncProxyName       DeclVariant
	RequestEncoderName  DeclVariant
	RequestDecoderName  DeclVariant
	ResponseEncoderName DeclVariant
	ResponseDecoderName DeclVariant
	ByteBufferType      string
	Methods             []Method
	FuzzingName         string
	TestBase            DeclName
}

// Protocol should be created using protocolInner.build().
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

	// Kind should always be default initialized.
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

func (inner protocolInner) build() *Protocol {
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

// methodInner contains information about a Method that should be filled out by
// the compiler.
type methodInner struct {
	// Private fields used to construct Method.
	protocolName      DeclName
	requestTypeShape  fidl.TypeShape
	responseTypeShape fidl.TypeShape
	// Public fields.
	fidl.Attributes
	Name                string
	Ordinal             uint64
	HasRequest          bool
	Request             []Parameter
	RequestCodingTable  DeclName
	HasResponse         bool
	Response            []Parameter
	ResponseCodingTable DeclName
	Transitional        bool
	Result              *Result
}

// Method should be created using methodInner.build().
// TODO: Consider factoring out common fields between Request and Response.
type Method struct {
	methodInner
	NameInLowerSnakeCase string
	// The name of a constant that defines the ordinal value.
	OrdinalName             string
	RequestSize             int
	RequestMaxHandles       int
	RequestMaxOutOfLine     int
	RequestByteBufferType   string
	RequestPadding          bool
	RequestFlexible         bool
	RequestHasPointer       bool
	RequestIsResource       bool
	ResponseSize            int
	ResponseMaxHandles      int
	ResponseMaxOutOfLine    int
	ResponseByteBufferType  string
	ResponseReceivedMaxSize int
	ResponsePadding         bool
	ResponseFlexible        bool
	ResponseHasPointer      bool
	ResponseIsResource      bool
	CallbackType            string
	ResponseHandlerType     string
	ResponderType           string
	LLProps                 LLProps
}

func (inner methodInner) build() Method {
	requestIsResource := false
	for _, p := range inner.Request {
		if p.Type.IsResource {
			requestIsResource = true
			break
		}
	}
	responseIsResource := false
	for _, p := range inner.Response {
		if p.Type.IsResource {
			responseIsResource = true
			break
		}
	}

	callbackType := ""
	if inner.HasResponse {
		callbackType = changeIfReserved(fidl.Identifier(inner.Name + "Callback"))
	}

	var computedResponseReceivedMaxSize int
	if inner.responseTypeShape.HasFlexibleEnvelope {
		computedResponseReceivedMaxSize = (1 << 32) - 1
	} else {
		computedResponseReceivedMaxSize = inner.responseTypeShape.InlineSize + inner.responseTypeShape.MaxOutOfLine
	}

	var responseBoundedness boundedness = boundednessBounded
	if inner.responseTypeShape.HasFlexibleEnvelope {
		responseBoundedness = boundednessUnbounded
	}

	m := Method{
		methodInner:             inner,
		NameInLowerSnakeCase:    fidl.ToSnakeCase(inner.Name),
		OrdinalName:             fmt.Sprintf("k%s_%s_Ordinal", inner.protocolName.Natural.Name(), inner.Name),
		RequestSize:             inner.requestTypeShape.InlineSize,
		RequestMaxHandles:       inner.requestTypeShape.MaxHandles,
		RequestMaxOutOfLine:     inner.requestTypeShape.MaxOutOfLine,
		RequestByteBufferType:   byteBufferType(inner.requestTypeShape.InlineSize, inner.requestTypeShape.MaxOutOfLine, boundednessBounded),
		RequestPadding:          inner.requestTypeShape.HasPadding,
		RequestFlexible:         inner.requestTypeShape.HasFlexibleEnvelope,
		RequestHasPointer:       inner.requestTypeShape.Depth > 0,
		RequestIsResource:       requestIsResource,
		ResponseSize:            inner.responseTypeShape.InlineSize,
		ResponseMaxHandles:      inner.responseTypeShape.MaxHandles,
		ResponseMaxOutOfLine:    inner.responseTypeShape.MaxOutOfLine,
		ResponseByteBufferType:  byteBufferType(inner.responseTypeShape.InlineSize, inner.responseTypeShape.MaxOutOfLine, responseBoundedness),
		ResponseReceivedMaxSize: computedResponseReceivedMaxSize,
		ResponsePadding:         inner.responseTypeShape.HasPadding,
		ResponseFlexible:        inner.responseTypeShape.HasFlexibleEnvelope,
		ResponseHasPointer:      inner.responseTypeShape.Depth > 0,
		ResponseIsResource:      responseIsResource,
		CallbackType:            callbackType,
		ResponseHandlerType:     fmt.Sprintf("%s_%s_ResponseHandler", inner.protocolName.Natural.Name(), inner.Name),
		ResponderType:           fmt.Sprintf("%s_%s_Responder", inner.protocolName.Natural.Name(), inner.Name),
	}
	m.LLProps = LLProps{
		ProtocolName:      inner.protocolName.Wire,
		LinearizeRequest:  len(inner.Request) > 0 && inner.requestTypeShape.Depth > 0,
		LinearizeResponse: len(inner.Response) > 0 && inner.responseTypeShape.Depth > 0,
		ClientContext:     m.buildLLContextProps(clientContext),
		ServerContext:     m.buildLLContextProps(serverContext),
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

// LLContextProps contain context-dependent properties of a method specific to llcpp.
// Context is client (write request and read response) or server (read request and write response).
type LLContextProps struct {
	// Should the request be allocated on the stack, in the managed flavor.
	StackAllocRequest bool
	// Should the response be allocated on the stack, in the managed flavor.
	StackAllocResponse bool
	// Total number of bytes of stack used for storing the request.
	StackUseRequest int
	// Total number of bytes of stack used for storing the response.
	StackUseResponse int
}

// LLProps contain properties of a method specific to llcpp
type LLProps struct {
	ProtocolName      DeclVariant
	LinearizeRequest  bool
	LinearizeResponse bool
	ClientContext     LLContextProps
	ServerContext     LLContextProps
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

// LLContext indicates where the request/response is used.
// The allocation strategies differ for client and server contexts.
type LLContext int

const (
	clientContext LLContext = iota
	serverContext LLContext = iota
)

func (m Method) buildLLContextProps(context LLContext) LLContextProps {
	stackAllocRequest := false
	stackAllocResponse := false
	if context == clientContext {
		stackAllocRequest = len(m.Request) == 0 || (m.RequestSize+m.RequestMaxOutOfLine) < llcppMaxStackAllocSize
		stackAllocResponse = len(m.Response) == 0 || (!m.ResponseFlexible && (m.ResponseSize+m.ResponseMaxOutOfLine) < llcppMaxStackAllocSize)
	} else {
		stackAllocRequest = len(m.Request) == 0 || (!m.RequestFlexible && (m.RequestSize+m.RequestMaxOutOfLine) < llcppMaxStackAllocSize)
		stackAllocResponse = len(m.Response) == 0 || (m.ResponseSize+m.ResponseMaxOutOfLine) < llcppMaxStackAllocSize
	}

	stackUseRequest := 0
	stackUseResponse := 0
	if stackAllocRequest {
		stackUseRequest = m.RequestSize + m.RequestMaxOutOfLine
	}
	if stackAllocResponse {
		stackUseResponse = m.ResponseSize + m.ResponseMaxOutOfLine
	}
	return LLContextProps{
		StackAllocRequest:  stackAllocRequest,
		StackAllocResponse: stackAllocResponse,
		StackUseRequest:    stackUseRequest,
		StackUseResponse:   stackUseResponse,
	}
}

func (c *compiler) compileProtocol(val fidl.Protocol) *Protocol {
	protocolName := c.compileDeclName(val.Name)
	codingTableName := codingTableName(val.Name)
	codingTableBase := DeclName{
		Wire:    NewDeclVariant(codingTableName, protocolName.Wire.Namespace()),
		Natural: NewDeclVariant(codingTableName, protocolName.Natural.Namespace().Append("_internal")),
	}
	methods := []Method{}
	maxResponseSize := 0
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

		method := methodInner{
			protocolName:        protocolName,
			requestTypeShape:    v.RequestTypeShapeV1,
			responseTypeShape:   v.ResponseTypeShapeV1,
			Attributes:          v.Attributes,
			Name:                name,
			Ordinal:             v.Ordinal,
			HasRequest:          v.HasRequest,
			Request:             c.compileParameterArray(v.Request),
			RequestCodingTable:  requestCodingTable,
			HasResponse:         v.HasResponse,
			Response:            c.compileParameterArray(v.Response),
			ResponseCodingTable: responseCodingTable,
			Transitional:        v.IsTransitional(),
			Result:              result,
		}.build()
		methods = append(methods, method)
		if size := method.ResponseSize + method.ResponseMaxOutOfLine; size > maxResponseSize {
			maxResponseSize = size
		}
	}

	fuzzingName := strings.ReplaceAll(strings.ReplaceAll(string(val.Name), ".", "_"), "/", "_")

	r := protocolInner{
		Attributes:          val.Attributes,
		DeclName:            protocolName,
		ClassName:           protocolName.AppendName("_clazz").Natural.Name(),
		ServiceName:         val.GetServiceName(),
		ProxyName:           protocolName.AppendName("_Proxy").Natural,
		StubName:            protocolName.AppendName("_Stub").Natural,
		EventSenderName:     protocolName.AppendName("_EventSender").Natural,
		SyncName:            protocolName.AppendName("_Sync").Natural,
		SyncProxyName:       protocolName.AppendName("_SyncProxy").Natural,
		RequestEncoderName:  protocolName.AppendName("_RequestEncoder").Natural,
		RequestDecoderName:  protocolName.AppendName("_RequestDecoder").Natural,
		ResponseEncoderName: protocolName.AppendName("_ResponseEncoder").Natural,
		ResponseDecoderName: protocolName.AppendName("_ResponseDecoder").Natural,
		ByteBufferType:      byteBufferType(maxResponseSize, 0, boundednessBounded),
		Methods:             methods,
		FuzzingName:         fuzzingName,
		TestBase:            protocolName.AppendName("_TestBase").AppendNamespace("testing"),
	}.build()
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

func byteBufferType(primarySize int, maxOutOfLine int, boundedness boundedness) string {
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
		return fmt.Sprintf("::fidl::internal::BoxedMessageBuffer<%s>", sizeString)
	}
	return fmt.Sprintf("::fidl::internal::InlineMessageBuffer<%s>", sizeString)
}
