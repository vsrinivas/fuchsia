// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

import (
	"fmt"
	"strings"

	fidl "go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

const (
	OutOfLineSuffix = "OutOfLine"
	InLineSuffix    = "InLine"
	RequestSuffix   = "Request"
	ResponseSuffix  = "Response"
	EventSuffix     = "Event"
	HandlesSuffix   = "Handles"
)

// Type represents a syzkaller type including type-options.
type Type string

// Enum represents a set of syzkaller flags
type Enum struct {
	Name    string
	Type    string
	Members []string
}

// Bits represents a set of syzkaller flags
type Bits struct {
	Name    string
	Type    string
	Members []string
}

// Struct represents a syzkaller struct.
type Struct struct {
	Name    string
	Members []StructMember
}

// StructMember represents a member of a syzkaller struct.
type StructMember struct {
	Name string
	Type Type
}

// Union represents a syzkaller union.
type Union struct {
	Name    string
	Members []StructMember
	VarLen  bool
}

// Protocol represents a FIDL protocol in terms of syzkaller structures.
type Protocol struct {
	Name string

	// ServiceNameString is the string service name for this FIDL protocol.
	ServiceNameString string

	// Methods is a list of methods for this FIDL protocol.
	Methods []Method
}

// Method represents a method of a FIDL protocol in terms of syzkaller syscalls.
type Method struct {
	// Ordinal is the ordinal for this method.
	Ordinal uint64

	// Name is the name of the Method, including the protocol name as a prefix.
	Name string

	// Request represents a struct containing the request parameters.
	Request *Struct

	// RequestHandles represents a struct containing the handles in the request parameters.
	RequestHandles *Struct

	// Response represents an optional struct containing the response parameters.
	Response *Struct

	// ResponseHandles represents a struct containing the handles in the response parameters.
	ResponseHandles *Struct

	// Structs contain all the structs generated during depth-first traversal of Request/Response.
	Structs []Struct

	// Unions contain all the unions generated during depth-first traversal of Request/Response.
	Unions []Union
}

// Root is the root of the syzkaller backend IR structure.
type Root struct {
	// Name is the name of the library.
	Name string

	// C header file path to be included in syscall description.
	HeaderPath string

	// Protocols represent the list of FIDL protocols represented as a collection of syskaller syscall descriptions.
	Protocols []Protocol

	// Structs correspond to syzkaller structs.
	Structs []Struct

	// Unions correspond to syzkaller unions.
	Unions []Union

	// Enums correspond to syzkaller flags.
	Enums []Enum

	// Bits correspond to syzkaller flags.
	Bits []Bits
}

type StructMap map[fidl.EncodedCompoundIdentifier]fidl.Struct
type UnionMap map[fidl.EncodedCompoundIdentifier]fidl.Union
type EnumMap map[fidl.EncodedCompoundIdentifier]fidl.Enum
type BitsMap map[fidl.EncodedCompoundIdentifier]fidl.Bits

type compiler struct {
	// decls contains all top-level declarations for the FIDL source.
	decls fidl.DeclInfoMap

	// structs contain all top-level struct definitions for the FIDL source.
	structs StructMap

	// unions contain all top-level union definitions for the FIDL source.
	unions UnionMap

	// enums contain all top-level enum definitions for the FIDL source.
	enums EnumMap

	// bits contain all top-level bits definitions for the FIDL source.
	bits BitsMap

	// library is the identifier for the current library.
	library fidl.LibraryIdentifier
}

var reservedWords = map[string]struct{}{
	"array":     {},
	"buffer":    {},
	"int8":      {},
	"int16":     {},
	"int32":     {},
	"int64":     {},
	"intptr":    {},
	"ptr":       {},
	"type":      {},
	"len":       {},
	"string":    {},
	"stringnoz": {},
	"const":     {},
	"in":        {},
	"out":       {},
	"flags":     {},
	"bytesize":  {},
	"bitsize":   {},
	"text":      {},
	"void":      {},
}

var primitiveTypes = map[fidl.PrimitiveSubtype]string{
	fidl.Bool:    "int8",
	fidl.Int8:    "int8",
	fidl.Int16:   "int16",
	fidl.Int32:   "int32",
	fidl.Int64:   "int64",
	fidl.Uint8:   "int8",
	fidl.Uint16:  "int16",
	fidl.Uint32:  "int32",
	fidl.Uint64:  "int64",
	fidl.Float32: "int32",
	fidl.Float64: "int64",
}

var handleSubtypes = map[fidl.HandleSubtype]string{
	fidl.Bti:          "zx_bti",
	fidl.Channel:      "zx_chan",
	fidl.Clock:        "zx_clock",
	fidl.DebugLog:     "zx_log",
	fidl.Event:        "zx_event",
	fidl.Eventpair:    "zx_eventpair",
	fidl.Exception:    "zx_exception",
	fidl.Fifo:         "zx_fifo",
	fidl.Guest:        "zx_guest",
	fidl.Handle:       "zx_handle",
	fidl.Interrupt:    "zx_interrupt",
	fidl.Iommu:        "zx_iommu",
	fidl.Job:          "zx_job",
	fidl.Pager:        "zx_pager",
	fidl.PciDevice:    "zx_pcidevice",
	fidl.Pmt:          "zx_pmt",
	fidl.Port:         "zx_port",
	fidl.Process:      "zx_process",
	fidl.Profile:      "zx_profile",
	fidl.Resource:     "zx_resource",
	fidl.Socket:       "zx_socket",
	fidl.Stream:       "zx_stream",
	fidl.SuspendToken: "zx_suspendtoken",
	fidl.Thread:       "zx_thread",
	fidl.Time:         "zx_timer",
	fidl.Vcpu:         "zx_vcpu",
	fidl.Vmar:         "zx_vmar",
	fidl.Vmo:          "zx_vmo",
}

func isReservedWord(str string) bool {
	_, ok := reservedWords[str]
	return ok
}

func changeIfReserved(val fidl.Identifier, ext string) string {
	str := string(val) + ext
	if isReservedWord(str) {
		return str + "_"
	}
	return str
}

func formatLibrary(library fidl.LibraryIdentifier, sep string) string {
	parts := []string{}
	for _, part := range library {
		parts = append(parts, string(part))
	}
	return changeIfReserved(fidl.Identifier(strings.Join(parts, sep)), "")
}

func formatLibraryPath(library fidl.LibraryIdentifier) string {
	return formatLibrary(library, "/")
}

func (c *compiler) compileIdentifier(id fidl.Identifier, ext string) string {
	str := string(id)
	str = fidl.ToSnakeCase(str)
	return changeIfReserved(fidl.Identifier(str), ext)
}

func (c *compiler) compileCompoundIdentifier(eci fidl.EncodedCompoundIdentifier, ext string) string {
	val := fidl.ParseCompoundIdentifier(eci)
	strs := []string{}
	strs = append(strs, formatLibrary(val.Library, "_"))
	strs = append(strs, changeIfReserved(val.Name, ext))
	return strings.Join(strs, "_")
}

func (c *compiler) compilePrimitiveSubtype(val fidl.PrimitiveSubtype) Type {
	// TODO(fxbug.dev/45007): Syzkaller does not support enum member references.
	// When this changes, we need to remove all special handling such as
	// ignoring specific files in the codegen test, or in the regen script.
	if t, ok := primitiveTypes[val]; ok {
		return Type(t)
	}
	panic(fmt.Sprintf("unknown primitive type: %v", val))
}

func (c *compiler) compilePrimitiveSubtypeRange(val fidl.PrimitiveSubtype, valRange string) Type {
	return Type(fmt.Sprintf("%s[%s]", c.compilePrimitiveSubtype(val), valRange))
}

func (c *compiler) compileHandleSubtype(val fidl.HandleSubtype) Type {
	if t, ok := handleSubtypes[val]; ok {
		return Type(t)
	}
	panic(fmt.Sprintf("unknown handle type: %v", val))
}

func (c *compiler) compileEnum(val fidl.Enum) Enum {
	e := Enum{
		c.compileCompoundIdentifier(val.Name, ""),
		string(c.compilePrimitiveSubtype(val.Type)),
		[]string{},
	}
	for _, v := range val.Members {
		e.Members = append(e.Members, fmt.Sprintf("%s_%s", e.Name, v.Name))
	}
	return e
}

func (c *compiler) compileBits(val fidl.Bits) Bits {
	e := Bits{
		c.compileCompoundIdentifier(val.Name, ""),
		string(c.compilePrimitiveSubtype(val.Type.PrimitiveSubtype)),
		[]string{},
	}
	for _, v := range val.Members {
		e.Members = append(e.Members, fmt.Sprintf("%s_%s", e.Name, v.Name))
	}
	return e
}

func (c *compiler) compileStructMember(p fidl.StructMember) (StructMember, *StructMember, *StructMember) {
	var i StructMember
	var o *StructMember
	var h *StructMember

	switch p.Type.Kind {
	case fidl.PrimitiveType:
		i = StructMember{
			Type: c.compilePrimitiveSubtype(p.Type.PrimitiveSubtype),
			Name: c.compileIdentifier(p.Name, ""),
		}
	case fidl.HandleType:
		i = StructMember{
			Type: Type("flags[fidl_handle_presence, int32]"),
			Name: c.compileIdentifier(p.Name, ""),
		}

		// Out-of-line handles
		h = &StructMember{
			Type: c.compileHandleSubtype(p.Type.HandleSubtype),
			Name: c.compileIdentifier(p.Name, ""),
		}
	case fidl.RequestType:
		i = StructMember{
			Type: Type("flags[fidl_handle_presence, int32]"),
			Name: c.compileIdentifier(p.Name, ""),
		}

		// Out-of-line handles
		h = &StructMember{
			Type: Type(fmt.Sprintf("zx_chan_%s_server", c.compileCompoundIdentifier(p.Type.RequestSubtype, ""))),
			Name: c.compileIdentifier(p.Name, ""),
		}
	case fidl.ArrayType:
		inLine, outOfLine, handle := c.compileStructMember(fidl.StructMember{
			Name: fidl.Identifier(c.compileIdentifier(p.Name, OutOfLineSuffix)),
			Type: (*p.Type.ElementType),
		})

		i = StructMember{
			Type: Type(fmt.Sprintf("array[%s, %v]", inLine.Type, *p.Type.ElementCount)),
			Name: c.compileIdentifier(p.Name, InLineSuffix),
		}

		// Variable-size, out-of-line data
		if outOfLine != nil {
			o = &StructMember{
				Type: Type(fmt.Sprintf("array[%s, %v]", outOfLine.Type, *p.Type.ElementCount)),
				Name: c.compileIdentifier(p.Name, OutOfLineSuffix),
			}
		}

		// Out-of-line handles
		if handle != nil {
			h = &StructMember{
				Type: Type(fmt.Sprintf("array[%s, %v]", handle.Type, *p.Type.ElementCount)),
				Name: c.compileIdentifier(p.Name, HandlesSuffix),
			}
		}
	case fidl.StringType:
		// Constant-size, in-line data
		i = StructMember{
			Type: Type("fidl_string"),
			Name: c.compileIdentifier(p.Name, InLineSuffix),
		}

		// Variable-size, out-of-line data
		o = &StructMember{
			Type: Type("fidl_aligned[stringnoz]"),
			Name: c.compileIdentifier(p.Name, OutOfLineSuffix),
		}
	case fidl.VectorType:
		// Constant-size, in-line data
		i = StructMember{
			Type: Type("fidl_vector"),
			Name: c.compileIdentifier(p.Name, InLineSuffix),
		}

		// Variable-size, out-of-line data
		inLine, outOfLine, handle := c.compileStructMember(fidl.StructMember{
			Name: fidl.Identifier(c.compileIdentifier(p.Name, OutOfLineSuffix)),
			Type: (*p.Type.ElementType),
		})
		o = &StructMember{
			Type: Type(fmt.Sprintf("array[%s]", inLine.Type)),
			Name: c.compileIdentifier(p.Name, OutOfLineSuffix),
		}

		if outOfLine != nil {
			o = &StructMember{
				Type: Type(fmt.Sprintf("parallel_array[%s, %s]", inLine.Type, outOfLine.Type)),
				Name: c.compileIdentifier(p.Name, OutOfLineSuffix),
			}
		}

		// Out-of-line handles
		if handle != nil {
			h = &StructMember{
				Type: Type(fmt.Sprintf("array[%s]", handle.Type)),
				Name: c.compileIdentifier(p.Name, ""),
			}
		}
	case fidl.IdentifierType:
		declInfo, ok := c.decls[p.Type.Identifier]
		if !ok {
			panic(fmt.Sprintf("unknown identifier: %v", p.Type.Identifier))
		}

		switch declInfo.Type {
		case fidl.EnumDeclType:
			i = StructMember{
				Type: Type(fmt.Sprintf("flags[%s, %s]", c.compileCompoundIdentifier(p.Type.Identifier, ""), c.compilePrimitiveSubtype(c.enums[p.Type.Identifier].Type))),
				Name: c.compileIdentifier(p.Name, ""),
			}
		case fidl.BitsDeclType:
			i = StructMember{
				Type: Type(fmt.Sprintf("flags[%s, %s]", c.compileCompoundIdentifier(p.Type.Identifier, ""), c.compilePrimitiveSubtype(c.bits[p.Type.Identifier].Type.PrimitiveSubtype))),
				Name: c.compileIdentifier(p.Name, ""),
			}
		case fidl.ProtocolDeclType:
			i = StructMember{
				Type: Type("flags[fidl_handle_presence, int32]"),
				Name: c.compileIdentifier(p.Name, ""),
			}

			// Out-of-line handles
			h = &StructMember{
				Type: Type(fmt.Sprintf("zx_chan_%s_client", c.compileCompoundIdentifier(p.Type.Identifier, ""))),
				Name: c.compileIdentifier(p.Name, ""),
			}
		case fidl.UnionDeclType:
			_, outOfLine, handles := c.compileUnion(c.unions[p.Type.Identifier])

			// Constant-size, in-line data
			t := c.compileCompoundIdentifier(p.Type.Identifier, InLineSuffix)
			i = StructMember{
				Type: Type(t),
				Name: c.compileIdentifier(p.Name, InLineSuffix),
			}

			// Variable-size, out-of-line data
			if outOfLine != nil {
				t := c.compileCompoundIdentifier(p.Type.Identifier, OutOfLineSuffix)
				o = &StructMember{
					Type: Type(t),
					Name: c.compileIdentifier(p.Name, OutOfLineSuffix),
				}
			}

			// Out-of-line handles
			if handles != nil {
				t := c.compileCompoundIdentifier(p.Type.Identifier, HandlesSuffix)
				h = &StructMember{
					Type: Type(t),
					Name: c.compileIdentifier(p.Name, ""),
				}
			}
		case fidl.StructDeclType:
			// Fixed-size, in-line data.
			i = StructMember{
				Type: Type(c.compileCompoundIdentifier(p.Type.Identifier, InLineSuffix)),
				Name: c.compileIdentifier(p.Name, InLineSuffix),
			}

			// Out-of-line data.
			o = &StructMember{
				Type: Type(c.compileCompoundIdentifier(p.Type.Identifier, OutOfLineSuffix)),
				Name: c.compileIdentifier(p.Name, OutOfLineSuffix),
			}

			// Handles.
			h = &StructMember{
				Type: Type(c.compileCompoundIdentifier(p.Type.Identifier, HandlesSuffix)),
				Name: c.compileIdentifier(p.Name, ""),
			}
		}
	}

	return i, o, h
}

func header(ordinal uint64) []StructMember {
	return []StructMember{
		{
			Type: Type(fmt.Sprintf("fidl_message_header[%d]", ordinal)),
			Name: "hdr",
		},
	}
}

type members []StructMember

func (members members) voidIfEmpty() members {
	if len(members) == 0 {
		return []StructMember{
			{Name: "void", Type: "void"},
		}
	}
	return members
}

func (members members) uint8PaddingIfEmpty() members {
	if len(members) == 0 {
		return []StructMember{
			{Name: "padding", Type: Type(primitiveTypes[fidl.Uint8])},
		}
	}
	return members
}

type result struct {
	Inline, OutOfLine, Handles members
}

func (c *compiler) compileStruct(p fidl.Struct) result {
	var result result
	for _, m := range p.Members {
		inLine, outOfLine, handles := c.compileStructMember(m)
		result.Inline = append(result.Inline, inLine)
		if outOfLine != nil {
			result.OutOfLine = append(result.OutOfLine, *outOfLine)
		}
		if handles != nil {
			result.Handles = append(result.Handles, *handles)
		}
	}
	return result
}

func (c *compiler) compileUnion(p fidl.Union) ([]StructMember, []StructMember, []StructMember) {
	var i, o, h []StructMember

	for _, m := range p.Members {
		if m.Reserved {
			continue
		}

		inLine, outOfLine, handles := c.compileStructMember(fidl.StructMember{
			Type: m.Type,
			Name: m.Name,
			FieldShapeV1: fidl.FieldShape{
				Offset:  m.Offset,
				Padding: 0,
			},
		})

		i = append(i, StructMember{
			Type: Type(fmt.Sprintf("fidl_union_member[%d, %s]", m.Ordinal, inLine.Type)),
			Name: inLine.Name,
		})

		if outOfLine != nil {
			o = append(o, *outOfLine)
		}

		if handles != nil {
			h = append(h, *handles)
		}
	}

	return i, o, h
}

func (c *compiler) compileParameters(name string, ordinal uint64, params []fidl.Parameter) (Struct, Struct) {
	var args fidl.Struct
	for _, p := range params {
		args.Members = append(args.Members, fidl.StructMember{
			Type:         p.Type,
			Name:         p.Name,
			FieldShapeV1: p.FieldShapeV1,
		})
	}
	result := c.compileStruct(args)
	return Struct{
			Name:    name,
			Members: append(append(header(ordinal), result.Inline...), result.OutOfLine...),
		}, Struct{
			Name:    name + HandlesSuffix,
			Members: result.Handles.voidIfEmpty(),
		}
}

func (c *compiler) compileMethod(protocolName fidl.EncodedCompoundIdentifier, val fidl.Method) Method {
	methodName := c.compileCompoundIdentifier(protocolName, string(val.Name))
	r := Method{
		Name:    methodName,
		Ordinal: val.Ordinal,
	}

	if val.HasRequest {
		request, requestHandles := c.compileParameters(r.Name+RequestSuffix, r.Ordinal, val.Request)
		r.Request = &request
		r.RequestHandles = &requestHandles
	}

	// For response, we only extract handles for now.
	if val.HasResponse {
		suffix := ResponseSuffix
		if !val.HasRequest {
			suffix = EventSuffix
		}
		response, responseHandles := c.compileParameters(r.Name+suffix, r.Ordinal, val.Response)
		r.Response = &response
		r.ResponseHandles = &responseHandles
	}

	return r
}

func (c *compiler) compileProtocol(val fidl.Protocol) Protocol {
	r := Protocol{
		Name:              c.compileCompoundIdentifier(val.Name, ""),
		ServiceNameString: strings.Trim(val.GetServiceName(), "\""),
	}
	for _, v := range val.Methods {
		r.Methods = append(r.Methods, c.compileMethod(val.Name, v))
	}
	return r
}

func compile(fidlData fidl.Root) Root {
	fidlData = fidlData.ForBindings("syzkaller")
	root := Root{}
	libraryName := fidl.ParseLibraryName(fidlData.Name)
	c := compiler{
		decls:   fidlData.DeclsWithDependencies(),
		structs: make(StructMap),
		unions:  make(UnionMap),
		enums:   make(EnumMap),
		bits:    make(BitsMap),
		library: libraryName,
	}

	root.HeaderPath = fmt.Sprintf("%s/c/fidl.h", formatLibraryPath(libraryName))

	for _, v := range fidlData.Enums {
		c.enums[v.Name] = v

		root.Enums = append(root.Enums, c.compileEnum(v))
	}

	for _, v := range fidlData.Bits {
		c.bits[v.Name] = v

		root.Bits = append(root.Bits, c.compileBits(v))
	}

	for _, v := range fidlData.Structs {
		// TODO(fxbug.dev/7704) remove once anonymous structs are supported
		if v.Anonymous {
			continue
		}
		c.structs[v.Name] = v

		result := c.compileStruct(v)
		root.Structs = append(root.Structs, Struct{
			Name:    c.compileCompoundIdentifier(v.Name, InLineSuffix),
			Members: result.Inline.uint8PaddingIfEmpty(),
		})

		root.Structs = append(root.Structs, Struct{
			Name:    c.compileCompoundIdentifier(v.Name, OutOfLineSuffix),
			Members: result.OutOfLine.voidIfEmpty(),
		})

		root.Structs = append(root.Structs, Struct{
			Name:    c.compileCompoundIdentifier(v.Name, HandlesSuffix),
			Members: result.Handles.voidIfEmpty(),
		})
	}

	for _, v := range fidlData.Unions {
		c.unions[v.Name] = v

		i, o, h := c.compileUnion(v)
		root.Unions = append(root.Unions, Union{
			Name:    c.compileCompoundIdentifier(v.Name, InLineSuffix),
			Members: i,
		})

		if len(o) == 0 {
			o = append(o, StructMember{
				Name: "void",
				Type: "void",
			})
		}

		if len(h) == 0 {
			h = append(h, StructMember{
				Name: "void",
				Type: "void",
			})
		}

		root.Unions = append(root.Unions, Union{
			Name:    c.compileCompoundIdentifier(v.Name, OutOfLineSuffix),
			Members: o,
			VarLen:  true,
		})

		root.Unions = append(root.Unions, Union{
			Name:    c.compileCompoundIdentifier(v.Name, HandlesSuffix),
			Members: h,
			VarLen:  true,
		})
	}

	for _, v := range fidlData.Protocols {
		root.Protocols = append(root.Protocols, c.compileProtocol(v))
	}

	exists := make(map[string]struct{})
	for _, i := range root.Protocols {
		for _, m := range i.Methods {
			for _, s := range m.Structs {
				if _, ok := exists[s.Name]; !ok {
					root.Structs = append(root.Structs, s)
					exists[s.Name] = struct{}{}
				}
			}
			for _, s := range m.Unions {
				if _, ok := exists[s.Name]; !ok {
					root.Unions = append(root.Unions, s)
					exists[s.Name] = struct{}{}
				}
			}
		}
	}

	return root
}
