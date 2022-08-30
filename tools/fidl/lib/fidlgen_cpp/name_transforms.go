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
	nFn nameTransform,
) declarationTransform {
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
	serviceMemberContext     = wireAndUnifedMemberContext(fidlgen.ToSnakeCase)
	serviceMemberTypeContext = wireAndUnifedMemberContext(fidlgen.ToUpperCamelCase)
	// Name of a constant
	// https://google.github.io/styleguide/cppguide.html#Constant_Names
	constantContext = declarationContext{
		NameContext: fidlgen.NewNameContext(),
		transforms: declarationTransforms{
			hlcpp:   simpleDeclarationTransform(hlcppNamespace, nil),
			unified: simpleDeclarationTransform(unifiedNamespace, fidlgen.ConstNameToKCamelCase),
			wire:    simpleDeclarationTransform(wireNamespace, fidlgen.ConstNameToKCamelCase),
		},
	}
	// Name of a data-type declaration
	// https://google.github.io/styleguide/cppguide.html#Type_Names
	typeContext = declarationContext{
		NameContext: fidlgen.NewNameContext(),
		transforms: declarationTransforms{
			hlcpp:   simpleDeclarationTransform(hlcppNamespace, nil),
			unified: simpleDeclarationTransform(unifiedNamespace, fidlgen.ToUpperCamelCase),
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
			hlcpp:   simpleDeclarationTransform(hlcppNamespace, nil),
			unified: simpleDeclarationTransform(unifiedNamespace, fidlgen.ToUpperCamelCase),
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
	// C++ keywords from: https://en.cppreference.com/w/cpp/keyword.
	cppKeywords := []string{
		"alignas",
		"alignof",
		"and_eq",
		"and",
		"asm",
		"atomic_cancel",
		"atomic_commit",
		"atomic_noexcept",
		"auto",
		"bitand",
		"bitor",
		"bool",
		"break",
		"case",
		"catch",
		"char",
		"char16_t",
		"char32_t",
		"class",
		"co_await",
		"co_return",
		"co_yield",
		"compl",
		"concept",
		"const_cast",
		"const",
		"consteval",
		"constexpr",
		"constinit",
		"continue",
		"decltype",
		"default",
		"delete",
		"do",
		"double",
		"dynamic_cast",
		"else",
		"enum",
		"explicit",
		"export",
		"extern",
		"false",
		"float",
		"for",
		"friend",
		"goto",
		"if",
		"inline",
		"int",
		"long",
		"module",
		"mutable",
		"namespace",
		"new",
		"noexcept",
		"not_eq",
		"not",
		"nullptr",
		"operator",
		"or_eq",
		"or",
		"private",
		"protected",
		"public",
		"reflexexpr",
		"register",
		"reinterpret_cast",
		"requires",
		"return",
		"short",
		"signed",
		"sizeof",
		"static_assert",
		"static_cast",
		"static",
		"struct",
		"switch",
		"synchronized",
		"template",
		"this",
		"thread_local",
		"throw",
		"true",
		"try",
		"typedef",
		"typeid",
		"typename",
		"union",
		"unsigned",
		"using",
		"virtual",
		"void",
		"volatile",
		"wchar_t",
		"while",
		"xor_eq",
		"xor",
	}

	// All names from errno definitions.
	errnos := []string{
		"EPERM",
		"ENOENT",
		"ESRCH",
		"EINTR",
		"EIO",
		"ENXIO",
		"E2BIG",
		"ENOEXEC",
		"EBADF",
		"ECHILD",
		"EAGAIN",
		"ENOMEM",
		"EACCES",
		"EFAULT",
		"ENOTBLK",
		"EBUSY",
		"EEXIST",
		"EXDEV",
		"ENODEV",
		"ENOTDIR",
		"EISDIR",
		"EINVAL",
		"ENFILE",
		"EMFILE",
		"ENOTTY",
		"ETXTBSY",
		"EFBIG",
		"ENOSPC",
		"ESPIPE",
		"EROFS",
		"EMLINK",
		"EPIPE",
		"EDOM",
		"ERANGE",
		"EDEADLK",
		"ENAMETOOLONG",
		"ENOLCK",
		"ENOSYS",
		"ENOTEMPTY",
		"ELOOP",
		"EWOULDBLOCK",
		"ENOMSG",
		"EIDRM",
		"ECHRNG",
		"EL2NSYNC",
		"EL3HLT",
		"EL3RST",
		"ELNRNG",
		"EUNATCH",
		"ENOCSI",
		"EL2HLT",
		"EBADE",
		"EBADR",
		"EXFULL",
		"ENOANO",
		"EBADRQC",
		"EBADSLT",
		"EDEADLOCK",
		"EBFONT",
		"ENOSTR",
		"ENODATA",
		"ETIME",
		"ENOSR",
		"ENONET",
		"ENOPKG",
		"EREMOTE",
		"ENOLINK",
		"EADV",
		"ESRMNT",
		"ECOMM",
		"EPROTO",
		"EMULTIHOP",
		"EDOTDOT",
		"EBADMSG",
		"EOVERFLOW",
		"ENOTUNIQ",
		"EBADFD",
		"EREMCHG",
		"ELIBACC",
		"ELIBBAD",
		"ELIBSCN",
		"ELIBMAX",
		"ELIBEXEC",
		"EILSEQ",
		"ERESTART",
		"ESTRPIPE",
		"EUSERS",
		"ENOTSOCK",
		"EDESTADDRREQ",
		"EMSGSIZE",
		"EPROTOTYPE",
		"ENOPROTOOPT",
		"EPROTONOSUPPORT",
		"ESOCKTNOSUPPORT",
		"EOPNOTSUPP",
		"ENOTSUP",
		"EPFNOSUPPORT",
		"EAFNOSUPPORT",
		"EADDRINUSE",
		"EADDRNOTAVAIL",
		"ENETDOWN",
		"ENETUNREACH",
		"ENETRESET",
		"ECONNABORTED",
		"ECONNRESET",
		"ENOBUFS",
		"EISCONN",
		"ENOTCONN",
		"ESHUTDOWN",
		"ETOOMANYREFS",
		"ETIMEDOUT",
		"ECONNREFUSED",
		"EHOSTDOWN",
		"EHOSTUNREACH",
		"EALREADY",
		"EINPROGRESS",
		"ESTALE",
		"EUCLEAN",
		"ENOTNAM",
		"ENAVAIL",
		"EISNAM",
		"EREMOTEIO",
		"EDQUOT",
		"ENOMEDIUM",
		"EMEDIUMTYPE",
		"ECANCELED",
		"ENOKEY",
		"EKEYEXPIRED",
		"EKEYREVOKED",
		"EKEYREJECTED",
		"EOWNERDEAD",
		"ENOTRECOVERABLE",
		"ERFKILL",
		"EHWPOISON",
	}

	// Names used in the C++ FIDL bindings.
	fidlNames := []string{
		"FidlType",
		"HandleEvents",
		"IsEmpty",
		"New",
		"Tag",
		"Unknown",
		"UnknownBytes",
		"UnknownData",
		"Which",
		"has_invalid_tag",
		"unknown",
		"which",
	}
	// TODO: Add "Builder", "ExternalBuilder"

	// Names used by ZX_ macros
	zxNames := []string{
		"ZX_ASSERT_LEVEL",
		"ZX_BTI_COMPRESS",
		"ZX_BTI_CONTIGUOUS",
		"ZX_BTI_PERM_EXECUTE",
		"ZX_BTI_PERM_READ",
		"ZX_BTI_PERM_WRITE",
		"ZX_CACHE_FLUSH_DATA",
		"ZX_CACHE_FLUSH_INSN",
		"ZX_CACHE_FLUSH_INVALIDATE",
		"ZX_CACHE_POLICY_CACHED",
		"ZX_CACHE_POLICY_MASK",
		"ZX_CACHE_POLICY_UNCACHED",
		"ZX_CACHE_POLICY_UNCACHED_DEVICE",
		"ZX_CACHE_POLICY_WRITE_COMBINING",
		"ZX_CHANNEL_MAX_MSG_BYTES",
		"ZX_CHANNEL_MAX_MSG_HANDLES",
		"ZX_CHANNEL_MAX_MSG_IOVECS",
		"ZX_CHANNEL_PEER_CLOSED",
		"ZX_CHANNEL_READABLE",
		"ZX_CHANNEL_READ_MAY_DISCARD",
		"ZX_CHANNEL_WRITABLE",
		"ZX_CHANNEL_WRITE_USE_IOVEC",
		"ZX_CLOCK_MONOTONIC",
		"ZX_CLOCK_STARTED",
		"ZX_CPRNG_ADD_ENTROPY_MAX_LEN",
		"ZX_CPRNG_DRAW_MAX_LEN",
		"ZX_CPU_SET_BITS_PER_WORD",
		"ZX_CPU_SET_MAX_CPUS",
		"ZX_DEBUG_ASSERT_IMPLEMENTED",
		"ZX_DEFAULT_BTI_RIGHTS",
		"ZX_DEFAULT_CHANNEL_RIGHTS",
		"ZX_DEFAULT_CLOCK_RIGHTS",
		"ZX_DEFAULT_EVENTPAIR_RIGHTS",
		"ZX_DEFAULT_EVENT_RIGHTS",
		"ZX_DEFAULT_EXCEPTION_RIGHTS",
		"ZX_DEFAULT_FIFO_RIGHTS",
		"ZX_DEFAULT_GUEST_RIGHTS",
		"ZX_DEFAULT_INTERRUPT_RIGHTS",
		"ZX_DEFAULT_IOMMU_RIGHTS",
		"ZX_DEFAULT_JOB_RIGHTS",
		"ZX_DEFAULT_LOG_RIGHTS",
		"ZX_DEFAULT_MSI_RIGHTS",
		"ZX_DEFAULT_PAGER_RIGHTS",
		"ZX_DEFAULT_PCI_DEVICE_RIGHTS",
		"ZX_DEFAULT_PCI_INTERRUPT_RIGHTS",
		"ZX_DEFAULT_PMT_RIGHTS",
		"ZX_DEFAULT_PORT_RIGHTS",
		"ZX_DEFAULT_PROCESS_RIGHTS",
		"ZX_DEFAULT_PROFILE_RIGHTS",
		"ZX_DEFAULT_RESOURCE_RIGHTS",
		"ZX_DEFAULT_SOCKET_RIGHTS",
		"ZX_DEFAULT_STREAM_RIGHTS",
		"ZX_DEFAULT_SUSPEND_TOKEN_RIGHTS",
		"ZX_DEFAULT_SYSTEM_EVENT_LOW_MEMORY_RIGHTS",
		"ZX_DEFAULT_THREAD_RIGHTS",
		"ZX_DEFAULT_TIMER_RIGHTS",
		"ZX_DEFAULT_VCPU_RIGHTS",
		"ZX_DEFAULT_VMAR_RIGHTS",
		"ZX_DEFAULT_VMO_RIGHTS",
		"ZX_ERR_ACCESS_DENIED",
		"ZX_ERR_ADDRESS_IN_USE",
		"ZX_ERR_ADDRESS_UNREACHABLE",
		"ZX_ERR_ALREADY_BOUND",
		"ZX_ERR_ALREADY_EXISTS",
		"ZX_ERR_ASYNC",
		"ZX_ERR_BAD_HANDLE",
		"ZX_ERR_BAD_PATH",
		"ZX_ERR_BAD_STATE",
		"ZX_ERR_BAD_SYSCALL",
		"ZX_ERR_BUFFER_TOO_SMALL",
		"ZX_ERR_CANCELED",
		"ZX_ERR_CONNECTION_ABORTED",
		"ZX_ERR_CONNECTION_REFUSED",
		"ZX_ERR_CONNECTION_RESET",
		"ZX_ERR_FILE_BIG",
		"ZX_ERR_INTERNAL",
		"ZX_ERR_INTERNAL_INTR_KILLED",
		"ZX_ERR_INTERNAL_INTR_RETRY",
		"ZX_ERR_INVALID_ARGS",
		"ZX_ERR_IO",
		"ZX_ERR_IO_DATA_INTEGRITY",
		"ZX_ERR_IO_DATA_LOSS",
		"ZX_ERR_IO_INVALID",
		"ZX_ERR_IO_MISSED_DEADLINE",
		"ZX_ERR_IO_NOT_PRESENT",
		"ZX_ERR_IO_OVERRUN",
		"ZX_ERR_IO_REFUSED",
		"ZX_ERR_NEXT",
		"ZX_ERR_NO_MEMORY",
		"ZX_ERR_NO_RESOURCES",
		"ZX_ERR_NO_SPACE",
		"ZX_ERR_NOT_CONNECTED",
		"ZX_ERR_NOT_DIR",
		"ZX_ERR_NOT_EMPTY",
		"ZX_ERR_NOT_FILE",
		"ZX_ERR_NOT_FOUND",
		"ZX_ERR_NOT_SUPPORTED",
		"ZX_ERR_OUT_OF_RANGE",
		"ZX_ERR_PEER_CLOSED",
		"ZX_ERR_PROTOCOL_NOT_SUPPORTED",
		"ZX_ERR_SHOULD_WAIT",
		"ZX_ERR_STOP",
		"ZX_ERR_TIMED_OUT",
		"ZX_ERR_UNAVAILABLE",
		"ZX_ERR_WRONG_TYPE",
		"ZX_EVENTPAIR_PEER_CLOSED",
		"ZX_EVENTPAIR_SIGNALED",
		"ZX_EVENTPAIR_SIGNAL_MASK",
		"ZX_EVENT_SIGNALED",
		"ZX_EVENT_SIGNAL_MASK",
		"ZX_EXCEPTION_STATE_HANDLED",
		"ZX_EXCEPTION_STATE_THREAD_EXIT",
		"ZX_EXCEPTION_STATE_TRY_NEXT",
		"ZX_EXCEPTION_STRATEGY_FIRST_CHANCE",
		"ZX_EXCEPTION_STRATEGY_SECOND_CHANCE",
		"ZX_FIFO_MAX_SIZE_BYTES",
		"ZX_FIFO_PEER_CLOSED",
		"ZX_FIFO_READABLE",
		"ZX_FIFO_WRITABLE",
		"ZX_HANDLE_FIXED_BITS_MASK",
		"ZX_HANDLE_INVALID",
		"ZX_HANDLE_OP_DUPLICATE",
		"ZX_HANDLE_OP_MOVE",
		"ZX_INFO_BTI",
		"ZX_INFO_CPU_STATS",
		"ZX_INFO_CPU_STATS_FLAG_ONLINE",
		"ZX_INFO_GUEST_STATS",
		"ZX_INFO_HANDLE_BASIC",
		"ZX_INFO_HANDLE_COUNT",
		"ZX_INFO_HANDLE_TABLE",
		"ZX_INFO_HANDLE_VALID",
		"ZX_INFO_INVALID_CPU",
		"ZX_INFO_JOB",
		"ZX_INFO_JOB_CHILDREN",
		"ZX_INFO_JOB_PROCESSES",
		"ZX_INFO_KMEM_STATS",
		"ZX_INFO_KMEM_STATS_EXTENDED",
		"ZX_INFO_MAPS_TYPE_ASPACE",
		"ZX_INFO_MAPS_TYPE_MAPPING",
		"ZX_INFO_MAPS_TYPE_NONE",
		"ZX_INFO_MAPS_TYPE_VMAR",
		"ZX_INFO_MSI",
		"ZX_INFO_NONE",
		"ZX_INFO_PROCESS",
		"ZX_INFO_PROCESS_FLAG_DEBUGGER_ATTACHED",
		"ZX_INFO_PROCESS_FLAG_EXITED",
		"ZX_INFO_PROCESS_FLAG_STARTED",
		"ZX_INFO_PROCESS_HANDLE_STATS",
		"ZX_INFO_PROCESS_MAPS",
		"ZX_INFO_PROCESS_THREADS",
		"ZX_INFO_PROCESS_VMOS",
		"ZX_INFO_PROCESS_VMOS_V1",
		"ZX_INFO_RESOURCE",
		"ZX_INFO_SOCKET",
		"ZX_INFO_STREAM",
		"ZX_INFO_TASK_RUNTIME",
		"ZX_INFO_TASK_RUNTIME_V1",
		"ZX_INFO_TASK_STATS",
		"ZX_INFO_THREAD",
		"ZX_INFO_THREAD_EXCEPTION_REPORT",
		"ZX_INFO_THREAD_EXCEPTION_REPORT_V1",
		"ZX_INFO_THREAD_STATS",
		"ZX_INFO_TIMER",
		"ZX_INFO_VCPU",
		"ZX_INFO_VCPU_FLAG_KICKED",
		"ZX_INFO_VMAR",
		"ZX_INFO_VMO",
		"ZX_INFO_VMO_CONTIGUOUS",
		"ZX_INFO_VMO_DISCARDABLE",
		"ZX_INFO_VMO_IMMUTABLE",
		"ZX_INFO_VMO_IS_COW_CLONE",
		"ZX_INFO_VMO_PAGER_BACKED",
		"ZX_INFO_VMO_RESIZABLE",
		"ZX_INFO_VMO_TYPE_PAGED",
		"ZX_INFO_VMO_TYPE_PHYSICAL",
		"ZX_INFO_VMO_V1",
		"ZX_INFO_VMO_VIA_HANDLE",
		"ZX_INFO_VMO_VIA_MAPPING",
		"ZX_INTERRUPT_BIND",
		"ZX_INTERRUPT_MAX_SLOTS",
		"ZX_INTERRUPT_MODE_DEFAULT",
		"ZX_INTERRUPT_MODE_EDGE_BOTH",
		"ZX_INTERRUPT_MODE_EDGE_HIGH",
		"ZX_INTERRUPT_MODE_EDGE_LOW",
		"ZX_INTERRUPT_MODE_LEVEL_HIGH",
		"ZX_INTERRUPT_MODE_LEVEL_LOW",
		"ZX_INTERRUPT_MODE_MASK",
		"ZX_INTERRUPT_REMAP_IRQ",
		"ZX_INTERRUPT_SLOT_USER",
		"ZX_INTERRUPT_UNBIND",
		"ZX_INTERRUPT_VIRTUAL",
		"ZX_JOB_CRITICAL_PROCESS_RETCODE_NONZERO",
		"ZX_JOB_NO_JOBS",
		"ZX_JOB_NO_PROCESSES",
		"ZX_JOB_TERMINATED",
		"ZX_KOID_FIRST",
		"ZX_KOID_INVALID",
		"ZX_KOID_KERNEL",
		"ZX_LOG_READABLE",
		"ZX_LOG_WRITABLE",
		"ZX_MAX_NAME_LEN",
		"ZX_MAX_PAGE_SHIFT",
		"ZX_MAX_PAGE_SIZE",
		"ZX_MIN_PAGE_SHIFT",
		"ZX_MIN_PAGE_SIZE",
		"ZX_MSI_MODE_MSI_X",
		"ZX_OBJ_TYPE_BTI",
		"ZX_OBJ_TYPE_CHANNEL",
		"ZX_OBJ_TYPE_CLOCK",
		"ZX_OBJ_TYPE_EVENT",
		"ZX_OBJ_TYPE_EVENTPAIR",
		"ZX_OBJ_TYPE_EXCEPTION",
		"ZX_OBJ_TYPE_FIFO",
		"ZX_OBJ_TYPE_GUEST",
		"ZX_OBJ_TYPE_INTERRUPT",
		"ZX_OBJ_TYPE_IOMMU",
		"ZX_OBJ_TYPE_JOB",
		"ZX_OBJ_TYPE_LOG",
		"ZX_OBJ_TYPE_MSI_ALLOCATION",
		"ZX_OBJ_TYPE_MSI_INTERRUPT",
		"ZX_OBJ_TYPE_NONE",
		"ZX_OBJ_TYPE_PAGER",
		"ZX_OBJ_TYPE_PCI_DEVICE",
		"ZX_OBJ_TYPE_PMT",
		"ZX_OBJ_TYPE_PORT",
		"ZX_OBJ_TYPE_PROCESS",
		"ZX_OBJ_TYPE_PROFILE",
		"ZX_OBJ_TYPE_RESOURCE",
		"ZX_OBJ_TYPE_SOCKET",
		"ZX_OBJ_TYPE_STREAM",
		"ZX_OBJ_TYPE_SUSPEND_TOKEN",
		"ZX_OBJ_TYPE_THREAD",
		"ZX_OBJ_TYPE_TIMER",
		"ZX_OBJ_TYPE_UPPER_BOUND",
		"ZX_OBJ_TYPE_VCPU",
		"ZX_OBJ_TYPE_VMAR",
		"ZX_OBJ_TYPE_VMO",
		"ZX_OK",
		"ZX_PAGE_MASK",
		"ZX_PAGER_OP_FAIL",
		"ZX_PAGER_VMO_COMPLETE",
		"ZX_PAGER_VMO_READ",
		"ZX_PAGE_SHIFT",
		"ZX_PAGE_SIZE",
		"ZX_PCI_BAR_REGS_PER_BRIDGE",
		"ZX_PCI_BAR_REGS_PER_DEVICE",
		"ZX_PCI_BAR_TYPE_MMIO",
		"ZX_PCI_BAR_TYPE_PIO",
		"ZX_PCI_BAR_TYPE_UNUSED",
		"ZX_PCI_BASE_CONFIG_SIZE",
		"ZX_PCI_ECAM_BYTE_PER_BUS",
		"ZX_PCIE_IRQ_MODE_DISABLED",
		"ZX_PCIE_IRQ_MODE_LEGACY",
		"ZX_PCIE_IRQ_MODE_LEGACY_NOACK",
		"ZX_PCIE_IRQ_MODE_MSI",
		"ZX_PCIE_IRQ_MODE_MSI_X",
		"ZX_PCI_EXTENDED_CONFIG_SIZE",
		"ZX_PCI_INIT_ARG_MAX_ECAM_WINDOWS",
		"ZX_PCI_INIT_ARG_MAX_SIZE",
		"ZX_PCI_INTERRUPT_SLOT",
		"ZX_PCI_MAX_BAR_REGS",
		"ZX_PCI_MAX_BUSSES",
		"ZX_PCI_MAX_DEVICES_PER_BUS",
		"ZX_PCI_MAX_FUNCTIONS_PER_BUS",
		"ZX_PCI_MAX_FUNCTIONS_PER_DEVICE",
		"ZX_PCI_MAX_IRQS",
		"ZX_PCI_MAX_LEGACY_IRQ_PINS",
		"ZX_PCI_MAX_MSI_IRQS",
		"ZX_PCI_MAX_MSIX_IRQS",
		"ZX_PCI_NO_IRQ_MAPPING",
		"ZX_PCI_STANDARD_CONFIG_HDR_SIZE",
		"ZX_PKT_GUEST_VCPU_INTERRUPT",
		"ZX_PKT_GUEST_VCPU_STARTUP",
		"ZX_PKT_TYPE_GUEST_BELL",
		"ZX_PKT_TYPE_GUEST_IO",
		"ZX_PKT_TYPE_GUEST_MEM",
		"ZX_PKT_TYPE_GUEST_VCPU",
		"ZX_PKT_TYPE_INTERRUPT",
		"ZX_PKT_TYPE_PAGE_REQUEST",
		"ZX_PKT_TYPE_SIGNAL_ONE",
		"ZX_PKT_TYPE_USER",
		"ZX_PORT_BIND_TO_INTERRUPT",
		// TODO(fxbug.dev/51002): These names occur both in zircon/vdso/profile.fidl
		// and zircon/system/public/zircon/syscalls/profile.h. If/when the latter
		// becomes generated from the former, we should remove these from the list
		// so they don't get escaped.
		"ZX_PRIORITY_DEFAULT",
		"ZX_PRIORITY_HIGH",
		"ZX_PRIORITY_HIGHEST",
		"ZX_PRIORITY_LOW",
		"ZX_PRIORITY_LOWEST",
		"ZX_PROCESS_DEBUG_ADDR_BREAK_ON_SET",
		"ZX_PROCESS_TERMINATED",
		"ZX_PROFILE_INFO_FLAG_CPU_MASK",
		"ZX_PROFILE_INFO_FLAG_DEADLINE",
		"ZX_PROFILE_INFO_FLAG_PRIORITY",
		"ZX_PROP_EXCEPTION_STATE",
		"ZX_PROP_EXCEPTION_STRATEGY",
		"ZX_PROP_JOB_KILL_ON_OOM",
		"ZX_PROP_NAME",
		"ZX_PROP_PROCESS_BREAK_ON_LOAD",
		"ZX_PROP_PROCESS_DEBUG_ADDR",
		"ZX_PROP_PROCESS_HW_TRACE_CONTEXT_ID",
		"ZX_PROP_PROCESS_VDSO_BASE_ADDRESS",
		"ZX_PROP_REGISTER_FS",
		"ZX_PROP_REGISTER_GS",
		"ZX_PROP_SOCKET_RX_THRESHOLD",
		"ZX_PROP_SOCKET_TX_THRESHOLD",
		"ZX_PROP_VMO_CONTENT_SIZE",
		"ZX_RIGHT_APPLY_PROFILE",
		"ZX_RIGHT_DESTROY",
		"ZX_RIGHT_DUPLICATE",
		"ZX_RIGHT_ENUMERATE",
		"ZX_RIGHT_EXECUTE",
		"ZX_RIGHT_GET_POLICY",
		"ZX_RIGHT_GET_PROPERTY",
		"ZX_RIGHT_INSPECT",
		"ZX_RIGHT_MANAGE_JOB",
		"ZX_RIGHT_MANAGE_PROCESS",
		"ZX_RIGHT_MANAGE_SOCKET",
		"ZX_RIGHT_MANAGE_THREAD",
		"ZX_RIGHT_MAP",
		"ZX_RIGHT_NONE",
		"ZX_RIGHT_READ",
		"ZX_RIGHT_SAME_RIGHTS",
		"ZX_RIGHTS_BASIC",
		"ZX_RIGHT_SET_POLICY",
		"ZX_RIGHT_SET_PROPERTY",
		"ZX_RIGHT_SIGNAL",
		"ZX_RIGHT_SIGNAL_PEER",
		"ZX_RIGHTS_IO",
		"ZX_RIGHTS_POLICY",
		"ZX_RIGHTS_PROPERTY",
		"ZX_RIGHT_TRANSFER",
		"ZX_RIGHT_WAIT",
		"ZX_RIGHT_WRITE",
		"ZX_SIGNAL_HANDLE_CLOSED",
		"ZX_SIGNAL_NONE",
		"ZX_SOCKET_CREATE_MASK",
		"ZX_SOCKET_DATAGRAM",
		"ZX_SOCKET_DISPOSITION_WRITE_DISABLED",
		"ZX_SOCKET_DISPOSITION_WRITE_ENABLED",
		"ZX_SOCKET_PEEK",
		"ZX_SOCKET_PEER_CLOSED",
		"ZX_SOCKET_PEER_WRITE_DISABLED",
		"ZX_SOCKET_READABLE",
		"ZX_SOCKET_READ_THRESHOLD",
		"ZX_SOCKET_STREAM",
		"ZX_SOCKET_WRITABLE",
		"ZX_SOCKET_WRITE_DISABLED",
		"ZX_SOCKET_WRITE_THRESHOLD",
		"ZX_STREAM_APPEND",
		"ZX_STREAM_CREATE_MASK",
		"ZX_STREAM_MODE_READ",
		"ZX_STREAM_MODE_WRITE",
		"ZX_STREAM_SEEK_ORIGIN_CURRENT",
		"ZX_STREAM_SEEK_ORIGIN_END",
		"ZX_STREAM_SEEK_ORIGIN_START",
		"ZX_SYSTEM_EVENT_IMMINENT_OUT_OF_MEMORY",
		"ZX_SYSTEM_EVENT_MEMORY_PRESSURE_CRITICAL",
		"ZX_SYSTEM_EVENT_MEMORY_PRESSURE_NORMAL",
		"ZX_SYSTEM_EVENT_MEMORY_PRESSURE_WARNING",
		"ZX_SYSTEM_EVENT_OUT_OF_MEMORY",
		"ZX_TASK_RETCODE_CRITICAL_PROCESS_KILL",
		"ZX_TASK_RETCODE_EXCEPTION_KILL",
		"ZX_TASK_RETCODE_OOM_KILL",
		"ZX_TASK_RETCODE_POLICY_KILL",
		"ZX_TASK_RETCODE_SYSCALL_KILL",
		"ZX_TASK_RETCODE_VDSO_KILL",
		"ZX_TASK_TERMINATED",
		"ZX_THREAD_RUNNING",
		"ZX_THREAD_STATE_BLOCKED",
		"ZX_THREAD_STATE_BLOCKED_CHANNEL",
		"ZX_THREAD_STATE_BLOCKED_EXCEPTION",
		"ZX_THREAD_STATE_BLOCKED_FUTEX",
		"ZX_THREAD_STATE_BLOCKED_INTERRUPT",
		"ZX_THREAD_STATE_BLOCKED_PAGER",
		"ZX_THREAD_STATE_BLOCKED_PORT",
		"ZX_THREAD_STATE_BLOCKED_SLEEPING",
		"ZX_THREAD_STATE_BLOCKED_WAIT_MANY",
		"ZX_THREAD_STATE_BLOCKED_WAIT_ONE",
		"ZX_THREAD_STATE_DEAD",
		"ZX_THREAD_STATE_DYING",
		"ZX_THREAD_STATE_NEW",
		"ZX_THREAD_STATE_RUNNING",
		"ZX_THREAD_STATE_SUSPENDED",
		"ZX_THREAD_SUSPENDED",
		"ZX_THREAD_TERMINATED",
		"ZX_TIME_INFINITE",
		"ZX_TIME_INFINITE_PAST",
		"ZX_TIMER_SIGNALED",
		"ZX_TIMER_SLACK_CENTER",
		"ZX_TIMER_SLACK_EARLY",
		"ZX_TIMER_SLACK_LATE",
		"ZX_USER_SIGNAL_0",
		"ZX_USER_SIGNAL_1",
		"ZX_USER_SIGNAL_2",
		"ZX_USER_SIGNAL_3",
		"ZX_USER_SIGNAL_4",
		"ZX_USER_SIGNAL_5",
		"ZX_USER_SIGNAL_6",
		"ZX_USER_SIGNAL_7",
		"ZX_USER_SIGNAL_ALL",
		"ZX_VM_ALIGN_128KB",
		"ZX_VM_ALIGN_128MB",
		"ZX_VM_ALIGN_16KB",
		"ZX_VM_ALIGN_16MB",
		"ZX_VM_ALIGN_1GB",
		"ZX_VM_ALIGN_1KB",
		"ZX_VM_ALIGN_1MB",
		"ZX_VM_ALIGN_256KB",
		"ZX_VM_ALIGN_256MB",
		"ZX_VM_ALIGN_2GB",
		"ZX_VM_ALIGN_2KB",
		"ZX_VM_ALIGN_2MB",
		"ZX_VM_ALIGN_32KB",
		"ZX_VM_ALIGN_32MB",
		"ZX_VM_ALIGN_4GB",
		"ZX_VM_ALIGN_4KB",
		"ZX_VM_ALIGN_4MB",
		"ZX_VM_ALIGN_512KB",
		"ZX_VM_ALIGN_512MB",
		"ZX_VM_ALIGN_64KB",
		"ZX_VM_ALIGN_64MB",
		"ZX_VM_ALIGN_8KB",
		"ZX_VM_ALIGN_8MB",
		"ZX_VM_ALIGN_BASE",
		"ZX_VM_ALLOW_FAULTS",
		"ZX_VMAR_OP_ALWAYS_NEED",
		"ZX_VMAR_OP_COMMIT",
		"ZX_VMAR_OP_DECOMMIT",
		"ZX_VMAR_OP_DONT_NEED",
		"ZX_VMAR_OP_MAP_RANGE",
		"ZX_VM_CAN_MAP_EXECUTE",
		"ZX_VM_CAN_MAP_READ",
		"ZX_VM_CAN_MAP_SPECIFIC",
		"ZX_VM_CAN_MAP_WRITE",
		"ZX_VM_COMPACT",
		"ZX_VM_FLAG_CAN_MAP_EXECUTE",
		"ZX_VM_FLAG_CAN_MAP_READ",
		"ZX_VM_FLAG_CAN_MAP_SPECIFIC",
		"ZX_VM_FLAG_CAN_MAP_WRITE",
		"ZX_VM_FLAG_COMPACT",
		"ZX_VM_FLAG_MAP_RANGE",
		"ZX_VM_FLAG_PERM_EXECUTE",
		"ZX_VM_FLAG_PERM_READ",
		"ZX_VM_FLAG_PERM_WRITE",
		"ZX_VM_FLAG_REQUIRE_NON_RESIZABLE",
		"ZX_VM_FLAG_SPECIFIC",
		"ZX_VM_FLAG_SPECIFIC_OVERWRITE",
		"ZX_VM_MAP_RANGE",
		"ZX_VMO_CHILD_NO_WRITE",
		"ZX_VMO_CHILD_RESIZABLE",
		"ZX_VMO_CHILD_SLICE",
		"ZX_VMO_CHILD_SNAPSHOT",
		"ZX_VMO_CHILD_SNAPSHOT_AT_LEAST_ON_WRITE",
		"ZX_VMO_DISCARDABLE",
		"ZX_VM_OFFSET_IS_UPPER_LIMIT",
		"ZX_VMO_OP_ALWAYS_NEED",
		"ZX_VMO_OP_CACHE_CLEAN",
		"ZX_VMO_OP_CACHE_CLEAN_INVALIDATE",
		"ZX_VMO_OP_CACHE_INVALIDATE",
		"ZX_VMO_OP_CACHE_SYNC",
		"ZX_VMO_OP_COMMIT",
		"ZX_VMO_OP_DECOMMIT",
		"ZX_VMO_OP_DONT_NEED",
		"ZX_VMO_OP_LOCK",
		"ZX_VMO_OP_TRY_LOCK",
		"ZX_VMO_OP_UNLOCK",
		"ZX_VMO_OP_ZERO",
		"ZX_VMO_RESIZABLE",
		"ZX_VMO_ZERO_CHILDREN",
		"ZX_VM_PERM_EXECUTE",
		"ZX_VM_PERM_READ",
		"ZX_VM_PERM_WRITE",
		"ZX_VM_REQUIRE_NON_RESIZABLE",
		"ZX_VM_SPECIFIC",
		"ZX_VM_SPECIFIC_OVERWRITE",
		"ZX_WAIT_ASYNC_EDGE",
		"ZX_WAIT_ASYNC_ONCE",
		"ZX_WAIT_ASYNC_TIMESTAMP",
		"ZX_WAIT_MANY_MAX_ITEMS",
	}

	// Non-ZX_ macro definitions defined in files that generated bindings outputs may import.
	// TODO(fxbug.dev/94622): this list is incomplete, downstream uses of some colliding names need
	// to be rectified first.
	macroNames := []string{
		"stderr",
		"stdin",
		"stdout",
	}

	// misc other names
	// TODO(ianloic): confirm which of these can be removed
	miscNames := []string{
		"assert",
		"import",
		"NULL",
		"offsetof",
		"xunion",
	}

	// Reserve names that are universally reserved:
	for _, ctx := range []declarationContext{
		constantContext,
		typeContext,
		serviceContext,
		protocolContext,
	} {
		ctx.ReserveNames(cppKeywords)
		ctx.ReserveNames(errnos)
		ctx.ReserveNames(fidlNames)
		ctx.ReserveNames(miscNames)
		ctx.ReserveNames(zxNames)
		ctx.ReserveNames(macroNames)
	}
	for _, ctx := range []memberContext{
		bitsMemberContext,
		enumMemberContext,
		structMemberContext,
		tableMemberContext,
		unionMemberContext,
		serviceMemberContext,
		serviceMemberTypeContext,
		methodNameContext,
	} {
		ctx.ReserveNames(cppKeywords)
		ctx.ReserveNames(errnos)
		ctx.ReserveNames(fidlNames)
		ctx.ReserveNames(miscNames)
		ctx.ReserveNames(zxNames)
		ctx.ReserveNames(macroNames)
	}
	nsComponentContext.ReserveNames(cppKeywords)
	nsComponentContext.ReserveNames(errnos)
	nsComponentContext.ReserveNames(miscNames)
	nsComponentContext.ReserveNames(zxNames)
	nsComponentContext.ReserveNames(macroNames)

	// A Clone() method on a type named Clone would conflict with its constructor.
	typeContext.ReserveNames([]string{"Clone"})

	structMemberContext.ReserveNames([]string{"Clone"})
	enumMemberContext.ReserveNames([]string{"Clone"})
	bitsMemberContext.ReserveNames([]string{"kMask"})
}
