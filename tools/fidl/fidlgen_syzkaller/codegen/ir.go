// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

import (
	"fmt"
	"log"
	"strings"

	"go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/common"
	"go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/types"
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

type StructMap map[types.EncodedCompoundIdentifier]types.Struct
type UnionMap map[types.EncodedCompoundIdentifier]types.Union
type EnumMap map[types.EncodedCompoundIdentifier]types.Enum
type BitsMap map[types.EncodedCompoundIdentifier]types.Bits

type compiler struct {
	// decls contains all top-level declarations for the FIDL source.
	decls types.DeclMap

	// structs contain all top-level struct definitions for the FIDL source.
	structs StructMap

	// unions contain all top-level union definitions for the FIDL source.
	unions UnionMap

	// enums contain all top-level enum definitions for the FIDL source.
	enums EnumMap

	// bits contain all top-level bits definitions for the FIDL source.
	bits BitsMap

	// library is the identifier for the current library.
	library types.LibraryIdentifier
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

var primitiveTypes = map[types.PrimitiveSubtype]string{
	types.Bool:    "int8",
	types.Int8:    "int8",
	types.Int16:   "int16",
	types.Int32:   "int32",
	types.Int64:   "int64",
	types.Uint8:   "int8",
	types.Uint16:  "int16",
	types.Uint32:  "int32",
	types.Uint64:  "int64",
	types.Float32: "int32",
	types.Float64: "int64",
}

var handleSubtypes = map[types.HandleSubtype]string{
	types.Bti:          "zx_bti",
	types.Channel:      "zx_chan",
	types.Clock:        "zx_clock",
	types.DebugLog:     "zx_log",
	types.Event:        "zx_event",
	types.Eventpair:    "zx_eventpair",
	types.Exception:    "zx_exception",
	types.Fifo:         "zx_fifo",
	types.Guest:        "zx_guest",
	types.Handle:       "zx_handle",
	types.Interrupt:    "zx_interrupt",
	types.Iommu:        "zx_iommu",
	types.Job:          "zx_job",
	types.Pager:        "zx_pager",
	types.PciDevice:    "zx_pcidevice",
	types.Pmt:          "zx_pmt",
	types.Port:         "zx_port",
	types.Process:      "zx_process",
	types.Profile:      "zx_profile",
	types.Resource:     "zx_resource",
	types.Socket:       "zx_socket",
	types.Stream:       "zx_stream",
	types.SuspendToken: "zx_suspendtoken",
	types.Thread:       "zx_thread",
	types.Time:         "zx_timer",
	types.Vcpu:         "zx_vcpu",
	types.Vmar:         "zx_vmar",
	types.Vmo:          "zx_vmo",
}

func isReservedWord(str string) bool {
	_, ok := reservedWords[str]
	return ok
}

func changeIfReserved(val types.Identifier, ext string) string {
	str := string(val) + ext
	if isReservedWord(str) {
		return str + "_"
	}
	return str
}

func formatLibrary(library types.LibraryIdentifier, sep string) string {
	parts := []string{}
	for _, part := range library {
		parts = append(parts, string(part))
	}
	return changeIfReserved(types.Identifier(strings.Join(parts, sep)), "")
}

func formatLibraryPath(library types.LibraryIdentifier) string {
	return formatLibrary(library, "/")
}

func (c *compiler) compileIdentifier(id types.Identifier, ext string) string {
	str := string(id)
	str = common.ToSnakeCase(str)
	return changeIfReserved(types.Identifier(str), ext)
}

func (c *compiler) compileCompoundIdentifier(eci types.EncodedCompoundIdentifier, ext string) string {
	val := types.ParseCompoundIdentifier(eci)
	strs := []string{}
	strs = append(strs, formatLibrary(val.Library, "_"))
	strs = append(strs, changeIfReserved(val.Name, ext))
	return strings.Join(strs, "_")
}

func (c *compiler) compilePrimitiveSubtype(val types.PrimitiveSubtype) Type {
	// TODO(fxbug.dev/45007): Syzkaller does not support enum member references.
	// When this changes, we need to remove all special handling such as
	// ignoring specific files in the codegen test, or in the regen script.
	t, ok := primitiveTypes[val]
	if !ok {
		log.Fatal("Unknown primitive type: ", val)
	}
	return Type(t)
}

func (c *compiler) compilePrimitiveSubtypeRange(val types.PrimitiveSubtype, valRange string) Type {
	return Type(fmt.Sprintf("%s[%s]", c.compilePrimitiveSubtype(val), valRange))
}

func (c *compiler) compileHandleSubtype(val types.HandleSubtype) Type {
	if t, ok := handleSubtypes[val]; ok {
		return Type(t)
	}
	log.Fatal("Unknown handle type: ", val)
	return Type("")
}

func (c *compiler) compileEnum(val types.Enum) Enum {
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

func (c *compiler) compileBits(val types.Bits) Bits {
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

func (c *compiler) compileStructMember(p types.StructMember) (StructMember, *StructMember, *StructMember) {
	var i StructMember
	var o *StructMember
	var h *StructMember

	switch p.Type.Kind {
	case types.PrimitiveType:
		i = StructMember{
			Type: c.compilePrimitiveSubtype(p.Type.PrimitiveSubtype),
			Name: c.compileIdentifier(p.Name, ""),
		}
	case types.HandleType:
		i = StructMember{
			Type: Type("flags[fidl_handle_presence, int32]"),
			Name: c.compileIdentifier(p.Name, ""),
		}

		// Out-of-line handles
		h = &StructMember{
			Type: c.compileHandleSubtype(p.Type.HandleSubtype),
			Name: c.compileIdentifier(p.Name, ""),
		}
	case types.RequestType:
		i = StructMember{
			Type: Type("flags[fidl_handle_presence, int32]"),
			Name: c.compileIdentifier(p.Name, ""),
		}

		// Out-of-line handles
		h = &StructMember{
			Type: Type(fmt.Sprintf("zx_chan_%s_server", c.compileCompoundIdentifier(p.Type.RequestSubtype, ""))),
			Name: c.compileIdentifier(p.Name, ""),
		}
	case types.ArrayType:
		inLine, outOfLine, handle := c.compileStructMember(types.StructMember{
			Name: types.Identifier(c.compileIdentifier(p.Name, OutOfLineSuffix)),
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
	case types.StringType:
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
	case types.VectorType:
		// Constant-size, in-line data
		i = StructMember{
			Type: Type("fidl_vector"),
			Name: c.compileIdentifier(p.Name, InLineSuffix),
		}

		// Variable-size, out-of-line data
		inLine, outOfLine, handle := c.compileStructMember(types.StructMember{
			Name: types.Identifier(c.compileIdentifier(p.Name, OutOfLineSuffix)),
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
	case types.IdentifierType:
		declType, ok := c.decls[p.Type.Identifier]
		if !ok {
			log.Fatal("Unknown identifier: ", p.Type.Identifier)
		}

		switch declType {
		case types.EnumDeclType:
			i = StructMember{
				Type: Type(fmt.Sprintf("flags[%s, %s]", c.compileCompoundIdentifier(p.Type.Identifier, ""), c.compilePrimitiveSubtype(c.enums[p.Type.Identifier].Type))),
				Name: c.compileIdentifier(p.Name, ""),
			}
		case types.BitsDeclType:
			i = StructMember{
				Type: Type(fmt.Sprintf("flags[%s, %s]", c.compileCompoundIdentifier(p.Type.Identifier, ""), c.compilePrimitiveSubtype(c.bits[p.Type.Identifier].Type.PrimitiveSubtype))),
				Name: c.compileIdentifier(p.Name, ""),
			}
		case types.ProtocolDeclType:
			i = StructMember{
				Type: Type("flags[fidl_handle_presence, int32]"),
				Name: c.compileIdentifier(p.Name, ""),
			}

			// Out-of-line handles
			h = &StructMember{
				Type: Type(fmt.Sprintf("zx_chan_%s_client", c.compileCompoundIdentifier(p.Type.Identifier, ""))),
				Name: c.compileIdentifier(p.Name, ""),
			}
		case types.UnionDeclType:
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
		case types.StructDeclType:
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
			{Name: "padding", Type: Type(primitiveTypes[types.Uint8])},
		}
	}
	return members
}

type result struct {
	Inline, OutOfLine, Handles members
}

func (c *compiler) compileStruct(p types.Struct) result {
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

func (c *compiler) compileUnion(p types.Union) ([]StructMember, []StructMember, []StructMember) {
	var i, o, h []StructMember

	for _, m := range p.Members {
		if m.Reserved {
			continue
		}

		inLine, outOfLine, handles := c.compileStructMember(types.StructMember{
			Type: m.Type,
			Name: m.Name,
			FieldShapeV1: types.FieldShape{
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

func (c *compiler) compileParameters(name string, ordinal uint64, params []types.Parameter) (Struct, Struct) {
	var args types.Struct
	for _, p := range params {
		args.Members = append(args.Members, types.StructMember{
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

func (c *compiler) compileMethod(protocolName types.EncodedCompoundIdentifier, val types.Method) Method {
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

func (c *compiler) compileProtocol(val types.Protocol) Protocol {
	r := Protocol{
		Name:              c.compileCompoundIdentifier(val.Name, ""),
		ServiceNameString: strings.Trim(val.GetServiceName(), "\""),
	}
	for _, v := range val.Methods {
		r.Methods = append(r.Methods, c.compileMethod(val.Name, v))
	}
	return r
}

func compile(fidlData types.Root) Root {
	fidlData = fidlData.ForBindings("syzkaller")
	root := Root{}
	libraryName := types.ParseLibraryName(fidlData.Name)
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
		// TODO(7704) remove once anonymous structs are supported
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
