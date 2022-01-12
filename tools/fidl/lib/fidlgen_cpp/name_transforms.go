// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"fmt"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

func changeIfReserved(name string, ctx fidlgen.NameContext) string {
	if ctx.IsReserved(name) {
		return name + "_"
	}
	return name
}

type nameTransform func(string) string

func (fn nameTransform) apply(s string) string {
	if fn == nil {
		return s
	}
	return fn(s)
}

type declarationTransform func(fidlgen.CompoundIdentifier, fidlgen.NameContext) name

func simpleDeclarationTransform(
	nsFn func(fidlgen.LibraryIdentifier) namespace,
	nFn nameTransform) declarationTransform {
	return func(id fidlgen.CompoundIdentifier, ctx fidlgen.NameContext) name {
		return nsFn(id.Library).member(changeIfReserved(nFn.apply(string(id.Name)), ctx))
	}
}

type declarationTransforms struct {
	hlcpp   declarationTransform
	unified declarationTransform
	wire    declarationTransform
}
type declarationContext struct {
	fidlgen.NameContext
	transforms declarationTransforms
}

func (ctx declarationContext) transform(id fidlgen.CompoundIdentifier) nameVariants {
	return nameVariants{
		HLCPP:   ctx.transforms.hlcpp(id, ctx.NameContext),
		Unified: ctx.transforms.unified(id, ctx.NameContext),
		Wire:    ctx.transforms.wire(id, ctx.NameContext),
	}
}

type memberTransform func(fidlgen.Identifier) name
type memberTransforms struct {
	hlcpp   nameTransform
	unified nameTransform
	wire    nameTransform
}
type memberContext struct {
	fidlgen.NameContext
	transforms memberTransforms
}

// wireAndUnifedMemberContext returns a memberContext that applies a transform
// to the wire and unified name, leaving HLCPP alone.
func wireAndUnifedMemberContext(transform nameTransform) memberContext {
	return memberContext{
		fidlgen.NewNameContext(), memberTransforms{unified: transform, wire: transform},
	}
}

// helper function used by transform
func (ctx memberContext) makeName(n string) name {
	return simpleName(changeIfReserved(n, ctx.NameContext))
}

func (ctx memberContext) transform(id fidlgen.Identifier) nameVariants {
	n := string(id)
	return nameVariants{
		HLCPP:   ctx.makeName(ctx.transforms.hlcpp.apply(n)),
		Unified: ctx.makeName(ctx.transforms.unified.apply(n)),
		Wire:    ctx.makeName(ctx.transforms.wire.apply(n)),
	}

}

// All data members are in lower_snake_case:
// https://google.github.io/styleguide/cppguide.html#Variable_Names
func newDataMemberContext() memberContext {
	return memberContext{
		fidlgen.NewNameContext(), memberTransforms{
			unified: fidlgen.ToSnakeCase,
			wire:    fidlgen.ToSnakeCase,
		},
	}
}

// All constants members are in kCamelCase:
// https://google.github.io/styleguide/cppguide.html#Enumerator_Names
func newConstantMemberContext() memberContext {
	return memberContext{
		NameContext: fidlgen.NewNameContext(),
		transforms: memberTransforms{
			unified: fidlgen.ConstNameToKCamelCase,
			wire:    fidlgen.ConstNameToKCamelCase,
		},
	}
}

var (
	// Name of a bits member
	bitsMemberContext = newConstantMemberContext()
	// Name of an enum member
	enumMemberContext = newConstantMemberContext()
	// Name of a struct member
	structMemberContext = newDataMemberContext()
	// Name of a table member
	tableMemberContext = newDataMemberContext()
	// Name of a union member
	unionMemberContext = newDataMemberContext()
	// Name of a union member tag
	unionMemberTagContext = memberContext{
		NameContext: fidlgen.NewNameContext(),
		transforms: memberTransforms{
			hlcpp:   fidlgen.ConstNameToKCamelCase,
			unified: fidlgen.ConstNameToKCamelCase,
			wire:    fidlgen.ConstNameToKCamelCase,
		},
	}
	// Name of a method
	// https://google.github.io/styleguide/cppguide.html#Function_Names
	methodNameContext = wireAndUnifedMemberContext(fidlgen.ToUpperCamelCase)
	// Name of a service member
	// https://google.github.io/styleguide/cppguide.html#Type_Names
	serviceMemberContext = wireAndUnifedMemberContext(fidlgen.ToSnakeCase)
	// Name of a constant
	// https://google.github.io/styleguide/cppguide.html#Constant_Names
	constantContext = declarationContext{
		NameContext: fidlgen.NewNameContext(),
		transforms: declarationTransforms{
			hlcpp:   simpleDeclarationTransform(hlcppNamespace, nil),
			unified: simpleDeclarationTransform(unifiedNamespace, nil),
			wire:    simpleDeclarationTransform(wireNamespace, fidlgen.ConstNameToKCamelCase),
		},
	}
	// Name of a data-type declaration
	// https://google.github.io/styleguide/cppguide.html#Type_Names
	typeContext = declarationContext{
		NameContext: fidlgen.NewNameContext(),
		transforms: declarationTransforms{
			hlcpp:   simpleDeclarationTransform(hlcppNamespace, nil),
			unified: simpleDeclarationTransform(unifiedNamespace, nil),
			wire:    simpleDeclarationTransform(wireNamespace, fidlgen.ToUpperCamelCase),
		},
	}
	// Name of a service declaration
	// https://google.github.io/styleguide/cppguide.html#Type_Names
	// TODO(yifeit): Protocols and services should not have DeclName.
	// Not all bindings generate these types.
	serviceContext = declarationContext{
		NameContext: fidlgen.NewNameContext(),
		transforms: declarationTransforms{
			hlcpp: simpleDeclarationTransform(hlcppNamespace, nil),
			// Intentionally using the natural namespace, since we would like
			// the unified bindings to transparently accept natural types, which
			// may certainly contain protocol endpoints.
			// TODO(fxbug.dev/72980): Switch to ClientEnd/ServerEnd and
			// underscore namespace when corresponding endpoint types can easily
			// convert into each other.
			unified: simpleDeclarationTransform(hlcppNamespace, nil),
			wire:    simpleDeclarationTransform(unifiedNamespace, fidlgen.ToUpperCamelCase),
		},
	}
	// Name of a protocol declaration
	// https://google.github.io/styleguide/cppguide.html#Type_Names
	// TODO(yifeit): Protocols and services should not have DeclName.
	// Not all bindings generate these types.
	protocolContext = declarationContext{
		NameContext: fidlgen.NewNameContext(),
		transforms: declarationTransforms{
			hlcpp:   simpleDeclarationTransform(hlcppNamespace, nil),
			unified: simpleDeclarationTransform(unifiedNamespace, fidlgen.ToUpperCamelCase),
			wire:    simpleDeclarationTransform(unifiedNamespace, fidlgen.ToUpperCamelCase),
		},
	}

	// Namespace components
	// https://google.github.io/styleguide/cppguide.html#Namespace_Names
	nsComponentContext = fidlgen.NewNameContext()
)

func declContext(declType fidlgen.DeclType) declarationContext {
	switch declType {
	case fidlgen.ConstDeclType:
		return constantContext
	case fidlgen.BitsDeclType, fidlgen.EnumDeclType, fidlgen.StructDeclType,
		fidlgen.TableDeclType, fidlgen.UnionDeclType:
		return typeContext
	case fidlgen.ProtocolDeclType:
		return protocolContext
	case fidlgen.ServiceDeclType:
		return serviceContext
	default:
		panic(fmt.Sprintf("Unknown decl type: %#v", declType))
	}
}

func memberNameContext(declType fidlgen.DeclType) memberContext {
	switch declType {
	case fidlgen.BitsDeclType:
		return bitsMemberContext
	case fidlgen.EnumDeclType:
		return enumMemberContext
	case fidlgen.StructDeclType:
		return structMemberContext
	case fidlgen.TableDeclType:
		return tableMemberContext
	case fidlgen.UnionDeclType:
		return unionMemberContext
	case fidlgen.ProtocolDeclType:
		return methodNameContext
	case fidlgen.ServiceDeclType:
		return serviceMemberContext
	default:
		panic(fmt.Sprintf("Decl type %#v shouldn't have members", declType))
	}
}

func init() {
	// C++ keywords from: https://en.cppreference.com/w/cpp/keyword
	cppKeywords := []string{
		"alignas", "alignof", "and_eq", "and", "asm", "atomic_cancel",
		"atomic_commit", "atomic_noexcept", "auto", "bitand", "bitor",
		"bool", "break", "case", "catch", "char", "char16_t", "char32_t",
		"class", "co_await", "co_return", "co_yield", "compl", "concept",
		"const_cast", "const", "consteval", "constexpr", "constinit",
		"continue", "decltype", "default", "delete", "do", "double",
		"dynamic_cast", "else", "enum", "explicit", "export", "extern",
		"false", "float", "for", "friend", "goto", "if", "inline", "int",
		"long", "module", "mutable", "namespace", "new", "noexcept",
		"not_eq", "not", "nullptr", "operator", "or_eq", "or", "private",
		"protected", "public", "reflexexpr", "register",
		"reinterpret_cast", "requires", "return", "short", "signed",
		"sizeof", "static_assert", "static_cast", "static", "struct",
		"switch", "synchronized", "template", "this", "thread_local",
		"throw", "true", "try", "typedef", "typeid", "typename", "union",
		"unsigned", "using", "virtual", "void", "volatile", "wchar_t",
		"while", "xor_eq", "xor"}

	// All names from errno definitions.
	errnos := []string{"EPERM", "ENOENT", "ESRCH", "EINTR", "EIO",
		"ENXIO", "E2BIG", "ENOEXEC", "EBADF", "ECHILD", "EAGAIN", "ENOMEM",
		"EACCES", "EFAULT", "ENOTBLK", "EBUSY", "EEXIST", "EXDEV", "ENODEV",
		"ENOTDIR", "EISDIR", "EINVAL", "ENFILE", "EMFILE", "ENOTTY",
		"ETXTBSY", "EFBIG", "ENOSPC", "ESPIPE", "EROFS", "EMLINK", "EPIPE",
		"EDOM", "ERANGE", "EDEADLK", "ENAMETOOLONG", "ENOLCK", "ENOSYS",
		"ENOTEMPTY", "ELOOP", "EWOULDBLOCK", "ENOMSG", "EIDRM", "ECHRNG",
		"EL2NSYNC", "EL3HLT", "EL3RST", "ELNRNG", "EUNATCH", "ENOCSI",
		"EL2HLT", "EBADE", "EBADR", "EXFULL", "ENOANO", "EBADRQC",
		"EBADSLT", "EDEADLOCK", "EBFONT", "ENOSTR", "ENODATA", "ETIME",
		"ENOSR", "ENONET", "ENOPKG", "EREMOTE", "ENOLINK", "EADV", "ESRMNT",
		"ECOMM", "EPROTO", "EMULTIHOP", "EDOTDOT", "EBADMSG", "EOVERFLOW",
		"ENOTUNIQ", "EBADFD", "EREMCHG", "ELIBACC", "ELIBBAD", "ELIBSCN",
		"ELIBMAX", "ELIBEXEC", "EILSEQ", "ERESTART", "ESTRPIPE", "EUSERS",
		"ENOTSOCK", "EDESTADDRREQ", "EMSGSIZE", "EPROTOTYPE", "ENOPROTOOPT",
		"EPROTONOSUPPORT", "ESOCKTNOSUPPORT", "EOPNOTSUPP", "ENOTSUP",
		"EPFNOSUPPORT", "EAFNOSUPPORT", "EADDRINUSE", "EADDRNOTAVAIL",
		"ENETDOWN", "ENETUNREACH", "ENETRESET", "ECONNABORTED",
		"ECONNRESET", "ENOBUFS", "EISCONN", "ENOTCONN", "ESHUTDOWN",
		"ETOOMANYREFS", "ETIMEDOUT", "ECONNREFUSED", "EHOSTDOWN",
		"EHOSTUNREACH", "EALREADY", "EINPROGRESS", "ESTALE", "EUCLEAN",
		"ENOTNAM", "ENAVAIL", "EISNAM", "EREMOTEIO", "EDQUOT", "ENOMEDIUM",
		"EMEDIUMTYPE", "ECANCELED", "ENOKEY", "EKEYEXPIRED", "EKEYREVOKED",
		"EKEYREJECTED", "EOWNERDEAD", "ENOTRECOVERABLE", "ERFKILL",
		"EHWPOISON"}

	// Names used in the C++ FIDL bindings
	fidlNames := []string{"FidlType", "HandleEvents",
		"has_invalid_tag", "IsEmpty", "New", "Tag", "unknown", "Unknown",
		"UnknownBytes", "UnknownData", "which", "Which"}

	// misc other names
	// TODO(ianloic): confirm which of these can be removed
	miscNames := []string{"assert", "import", "NULL", "offsetof",
		"xunion"}

	// Reserve names that are universally reserved:
	for _, ctx := range []declarationContext{constantContext, typeContext,
		serviceContext, protocolContext} {
		ctx.ReserveNames(cppKeywords)
		ctx.ReserveNames(errnos)
		ctx.ReserveNames(fidlNames)
		ctx.ReserveNames(miscNames)
	}
	for _, ctx := range []memberContext{
		bitsMemberContext, enumMemberContext, structMemberContext,
		tableMemberContext, unionMemberContext, serviceMemberContext,
		methodNameContext} {
		ctx.ReserveNames(cppKeywords)
		ctx.ReserveNames(errnos)
		ctx.ReserveNames(fidlNames)
		ctx.ReserveNames(miscNames)
	}
	nsComponentContext.ReserveNames(cppKeywords)
	nsComponentContext.ReserveNames(errnos)
	nsComponentContext.ReserveNames(fidlNames)
	nsComponentContext.ReserveNames(miscNames)

	// A Clone() method on a type named Clone would conflict with its constructor.
	typeContext.ReserveNames([]string{"Clone"})

	structMemberContext.ReserveNames([]string{"Clone"})
	enumMemberContext.ReserveNames([]string{"Clone"})
	bitsMemberContext.ReserveNames([]string{"kMask"})
}
