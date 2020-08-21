// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxb/55387): Temporary, will be removed.

package types

import (
	"go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/types"
)

func ReadJSONIr(filename string) (Root, error) {
	return types.ReadJSONIr(filename)
}

type Identifier = types.Identifier
type LibraryIdentifier = types.LibraryIdentifier
type CompoundIdentifier = types.CompoundIdentifier
type EncodedLibraryIdentifier = types.EncodedLibraryIdentifier
type EncodedCompoundIdentifier = types.EncodedCompoundIdentifier

func ParseLibraryName(eli EncodedLibraryIdentifier) LibraryIdentifier {
	return types.ParseLibraryName(eli)
}
func ParseCompoundIdentifier(eci EncodedCompoundIdentifier) CompoundIdentifier {
	return types.ParseCompoundIdentifier(eci)
}

//func EnsureLibrary = types.EnsureLibrary
type PrimitiveSubtype = types.PrimitiveSubtype

const (
	Bool    = types.Bool
	Int8    = types.Int8
	Int16   = types.Int16
	Int32   = types.Int32
	Int64   = types.Int64
	Uint8   = types.Uint8
	Uint16  = types.Uint16
	Uint32  = types.Uint32
	Uint64  = types.Uint64
	Float32 = types.Float32
	Float64 = types.Float64
)

type HandleSubtype = types.HandleSubtype

const (
	Handle       = types.Handle
	Bti          = types.Bti
	Channel      = types.Channel
	Clock        = types.Clock
	DebugLog     = types.DebugLog
	Event        = types.Event
	Eventpair    = types.Eventpair
	Exception    = types.Exception
	Fifo         = types.Fifo
	Guest        = types.Guest
	Interrupt    = types.Interrupt
	Iommu        = types.Iommu
	Job          = types.Job
	Pager        = types.Pager
	PciDevice    = types.PciDevice
	Pmt          = types.Pmt
	Port         = types.Port
	Process      = types.Process
	Profile      = types.Profile
	Resource     = types.Resource
	Socket       = types.Socket
	Stream       = types.Stream
	SuspendToken = types.SuspendToken
	Thread       = types.Thread
	Time         = types.Time
	Vcpu         = types.Vcpu
	Vmar         = types.Vmar
	Vmo          = types.Vmo
)

type ObjectType = types.ObjectType

const (
	ObjectTypeNone         = types.ObjectTypeNone
	ObjectTypeProcess      = types.ObjectTypeProcess
	ObjectTypeThread       = types.ObjectTypeThread
	ObjectTypeVmo          = types.ObjectTypeVmo
	ObjectTypeChannel      = types.ObjectTypeChannel
	ObjectTypeEvent        = types.ObjectTypeEvent
	ObjectTypePort         = types.ObjectTypePort
	ObjectTypeInterrupt    = types.ObjectTypeInterrupt
	ObjectTypePciDevice    = types.ObjectTypePciDevice
	ObjectTypeLog          = types.ObjectTypeLog
	ObjectTypeSocket       = types.ObjectTypeSocket
	ObjectTypeResource     = types.ObjectTypeResource
	ObjectTypeEventPair    = types.ObjectTypeEventPair
	ObjectTypeJob          = types.ObjectTypeJob
	ObjectTypeVmar         = types.ObjectTypeVmar
	ObjectTypeFifo         = types.ObjectTypeFifo
	ObjectTypeGuest        = types.ObjectTypeGuest
	ObjectTypeVcpu         = types.ObjectTypeVcpu
	ObjectTypeTimer        = types.ObjectTypeTimer
	ObjectTypeIommu        = types.ObjectTypeIommu
	ObjectTypeBti          = types.ObjectTypeBti
	ObjectTypeProfile      = types.ObjectTypeProfile
	ObjectTypePmt          = types.ObjectTypePmt
	ObjectTypeSuspendToken = types.ObjectTypeSuspendToken
	ObjectTypePager        = types.ObjectTypePager
)

//func ObjectTypeFromHandleSubtype = types.ObjectTypeFromHandleSubtype
type HandleRights = types.HandleRights
type LiteralKind = types.LiteralKind

const (
	StringLiteral  = types.StringLiteral
	NumericLiteral = types.NumericLiteral
	TrueLiteral    = types.TrueLiteral
	FalseLiteral   = types.FalseLiteral
	DefaultLiteral = types.DefaultLiteral
)

type Literal = types.Literal
type ConstantKind = types.ConstantKind

const (
	IdentifierConstant = types.IdentifierConstant
	LiteralConstant    = types.LiteralConstant
)

type Constant = types.Constant
type TypeKind = types.TypeKind

const (
	ArrayType      = types.ArrayType
	VectorType     = types.VectorType
	StringType     = types.StringType
	HandleType     = types.HandleType
	RequestType    = types.RequestType
	PrimitiveType  = types.PrimitiveType
	IdentifierType = types.IdentifierType
)

type Type = types.Type
type Attribute = types.Attribute
type Attributes = types.Attributes
type TypeShape = types.TypeShape
type FieldShape = types.FieldShape
type Union = types.Union
type UnionMember = types.UnionMember
type Table = types.Table
type TableMember = types.TableMember
type Struct = types.Struct
type StructMember = types.StructMember

func EmptyStructMember(name string) StructMember {
	return types.EmptyStructMember(name)
}

type Protocol = types.Protocol
type Interface = types.Interface
type Service = types.Service
type ServiceMember = types.ServiceMember
type Method = types.Method
type Parameter = types.Parameter
type Enum = types.Enum
type EnumMember = types.EnumMember
type Bits = types.Bits
type BitsMember = types.BitsMember
type Const = types.Const
type Strictness = types.Strictness

const (
	IsFlexible = types.IsFlexible
	IsStrict   = types.IsStrict
)

type DeclType = types.DeclType

const (
	ConstDeclType     = types.ConstDeclType
	BitsDeclType      = types.BitsDeclType
	EnumDeclType      = types.EnumDeclType
	ProtocolDeclType  = types.ProtocolDeclType
	ServiceDeclType   = types.ServiceDeclType
	StructDeclType    = types.StructDeclType
	TableDeclType     = types.TableDeclType
	UnionDeclType     = types.UnionDeclType
	InterfaceDeclType = types.InterfaceDeclType
)

type DeclMap = types.DeclMap
type Library = types.Library
type Root = types.Root
