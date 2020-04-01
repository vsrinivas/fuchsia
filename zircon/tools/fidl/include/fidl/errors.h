// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_ERRORS_H_
#define ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_ERRORS_H_

#include <cassert>
#include <set>
#include <string_view>

#include "source_span.h"

namespace fidl {

// Forward decls
namespace raw {
class Attribute;
class AttributeList;
}  // namespace raw

namespace flat {
struct Constant;
struct IdentifierConstant;
struct LiteralConstant;
struct TypeConstructor;
struct Type;
class TypeTemplate;
class Name;
}  // namespace flat

constexpr int count_format_args(std::string_view s, size_t i = 0) {
  if (i + 1 >= s.size()) {
    return 0;
  }
  int extra = 0;
  if (s[i] == '{' && s[i + 1] == '}') {
    extra = 1;
  }
  return extra + count_format_args(s, i + 1);
}

template <typename... Args>
struct Error {
  std::string_view msg;

  constexpr Error(std::string_view msg) : msg(msg) {
    assert(sizeof...(Args) == count_format_args(msg) &&
           "number of format string parameters '{}' != number of template arguments");
  }
};

// ---------------------------------------------------------------------------
// Library::ConsumeFile: Consume* methods and declaration registration
// ---------------------------------------------------------------------------
constexpr Error<flat::Name> ErrNameCollision(
    "Name collision: {}");
constexpr Error<flat::Name> ErrDeclNameConflictsWithLibraryImport(
    "Declaration name '{}' conflicts with a library import; consider using the "
    "'as' keyword to import the library under a different name.");
constexpr Error ErrFilesDisagreeOnLibraryName(
    "Two files in the library disagree about the name of the library");
constexpr Error<std::vector<std::string_view>> ErrDuplicateLibraryImport(
    "Library {} already imported. Did you require it twice?");
constexpr Error<raw::AttributeList *> ErrAttributesNotAllowedOnLibraryImport(
    "no attributes allowed on library import, found: {}");
constexpr Error<std::vector<std::string_view>> ErrUnknownLibrary(
    "Could not find library named {}. Did you include its sources with --files?");
constexpr Error ErrProtocolComposedMultipleTimes(
    "protocol composed multiple times");
constexpr Error ErrDefaultsOnTablesNotSupported(
    "Defaults on tables are not yet supported.");
constexpr Error ErrNullableTableMember(
    "Table members cannot be nullable");
constexpr Error ErrNullableUnionMember(
    "union members cannot be nullable");

// ---------------------------------------------------------------------------
// Library::Compile: SortDeclarations
// ---------------------------------------------------------------------------
constexpr Error<flat::Name> ErrFailedConstantLookup(
    "Unable to find the constant named: {}");
constexpr Error ErrIncludeCycle(
    "There is an includes-cycle in declarations");

// ---------------------------------------------------------------------------
// Library::Compile: Compilation, Resolution, Validation
// ---------------------------------------------------------------------------
constexpr Error<std::vector<std::string_view>, std::vector<std::string_view>>
ErrUnknownDependentLibrary(
    "Unknown dependent library {} or reference to member of "
    "library {}. Did you require it with `using`?");
constexpr Error<const flat::Type *> ErrInvalidConstantType(
    "invalid constant type {}");
constexpr Error ErrCannotResolveConstantValue(
    "unable to resolve constant value");
constexpr Error ErrOrOperatorOnNonPrimitiveValue(
    "Or operator can only be applied to primitive-kinded values");
constexpr Error ErrUnknownEnumMember(
    "Unknown enum member");
constexpr Error ErrUnknownBitsMember(
    "Unknown bits member");
constexpr Error<flat::IdentifierConstant *> ErrExpectedValueButGotType(
    "{} is a type, but a value was expected");
constexpr Error<flat::Name, flat::Name> ErrMismatchedNameTypeAssignment(
    "mismatched named type assignment: cannot define a constant or default value of type {} "
    "using a value of type {}");
constexpr Error<flat::IdentifierConstant *, const flat::TypeConstructor *, const flat::Type *>
ErrCannotConvertConstantToType(
    "{}, of type {}, cannot be converted to type {}");
constexpr Error<flat::LiteralConstant *, uint64_t, const flat::Type *>
ErrStringConstantExceedsSizeBound(
    "{} (string:{}) exceeds the size bound of type {}");
constexpr Error<flat::LiteralConstant *, const flat::Type *> ErrConstantCannotBeInterpretedAsType(
    "{} cannot be interpreted as type {}");
constexpr Error ErrCouldNotResolveIdentifierToType(
    "could not resolve identifier to a type");
constexpr Error<const flat::Type *> ErrBitsTypeMustBeUnsignedIntegralPrimitive(
    "bits may only be of unsigned integral primitive type, found {}");
constexpr Error<const flat::Type *> ErrEnumTypeMustBeIntegralPrimitive(
    "enums may only be of integral primitive type, found {}");
constexpr Error ErrUnknownAttributeOnInvalidType(
    "[Unknown] attribute can be only be used on flexible or [Transitional] types.");
constexpr Error ErrUnknownAttributeOnMultipleMembers(
    "[Unknown] attribute can be only applied to one member.");
constexpr Error<flat::Name> ErrUnknownType(
    "unknown type {}");
constexpr Error ErrComposingNonProtocol(
    "This declaration is not a protocol");
constexpr Error<SourceSpan> ErrDuplicateMethodName(
    "Multiple methods with the same name in a protocol; last occurrence was at {}");
constexpr Error ErrZeroValueOrdinal(
    "Ordinal value 0 disallowed.");
constexpr Error<SourceSpan, std::string> ErrDuplicateMethodOrdinal(
    "Multiple methods with the same ordinal in a protocol; previous was at {}. "
    "Consider using attribute [Selector=\"{}\"] to change the name used to "
    "calculate the ordinal.");
constexpr Error ErrDuplicateMethodParameterName(
    "Multiple parameters with the same name in a method");
constexpr Error<SourceSpan> ErrDuplicateServiceMemberName(
    "multiple service members with the same name; previous was at {}");
constexpr Error ErrNonProtocolServiceMember(
    "only protocol members are allowed");
constexpr Error ErrNullableServiceMember(
    "service members cannot be nullable");
constexpr Error<SourceSpan> ErrDuplicateStructMemberName(
    "Multiple struct fields with the same name; previous was at {}");
constexpr Error<std::string, const flat::Type *> ErrInvalidStructMemberType(
    "struct field {} has an invalid default type{}");
constexpr Error<SourceSpan> ErrDuplicateTableFieldOrdinal(
    "Multiple table fields with the same ordinal; previous was at {}");
constexpr Error<SourceSpan> ErrDuplicateTableFieldName(
    "Multiple table fields with the same name; previous was at {}");
constexpr Error<uint32_t> ErrNonDenseOrdinalInTable(
    "missing ordinal {} (ordinals must be dense); consider marking it reserved");
constexpr Error<SourceSpan> ErrDuplicateUnionMemberOrdinal(
    "Multiple union fields with the same ordinal; previous was at {}");
constexpr Error<SourceSpan> ErrDuplicateUnionMemberName(
    "Multiple union members with the same name; previous was at {}");
constexpr Error<uint32_t> ErrNonDenseOrdinalInUnion(
    "missing ordinal {} (ordinals must be dense); consider marking it reserved");
constexpr Error ErrCouldNotResolveHandleRights(
    "unable to resolve handle rights");
constexpr Error ErrCouldNotParseSizeBound(
    "unable to parse size bound");
constexpr Error<std::string> ErrCouldNotResolveMember(
    "unable to resolve {} member");
constexpr Error<std::string, std::string, flat::Name> ErrDuplicateMemberName(
    "name of member {} conflicts with previously declared member in the {} {}");
constexpr Error<std::string, std::string, std::string, flat::Name> ErrDuplicateMemberValue(
    "value of member {} conflicts with previously declared member {} in the {} {}");

// ---------------------------------------------------------------------------
// Attribute Validation: Placement, Values, Constraints
// ---------------------------------------------------------------------------
constexpr Error<raw::Attribute> ErrInvalidAttributePlacement(
    "placement of attribute '{}' disallowed here");
constexpr Error<raw::Attribute, std::string, std::set<std::string>> ErrInvalidAttributeValue(
    "attribute '{}' has invalid value '{}', should be one of '{}'");
constexpr Error<raw::Attribute, std::string> ErrAttributeConstraintNotSatisfied(
    "declaration did not satisfy constraint of attribute '{}' with value '{}'");
constexpr Error<flat::Name> ErrUnionCannotBeSimple(
    "union '{}' is not allowed to be simple");
constexpr Error<std::string> ErrStructMemberMustBeSimple(
    "member '{}' is not simple");
constexpr Error<uint32_t, uint32_t> ErrTooManyBytes(
    "too large: only {} bytes allowed, but {} bytes found");
constexpr Error<uint32_t, uint32_t> ErrTooManyHandles(
    "too many handles: only {} allowed, but {} found");
constexpr Error ErrInvalidErrorType(
    "invalid error type: must be int32, uint32 or an enum therof");
constexpr Error<std::string, std::set<std::string>> ErrInvalidTransportType(
    "invalid transport type: got {} expected one of {}");
constexpr Error ErrBoundIsTooBig(
    "bound is too big");
constexpr Error<std::string> ErrUnableToParseBound(
    "unable to parse bound '{}'");
constexpr Error<std::string, std::string> WarnAttributeTypo(
    "suspect attribute with name '{}'; did you mean '{}'?");

// ---------------------------------------------------------------------------
// Type Templates
// ---------------------------------------------------------------------------
constexpr Error<flat::Name> ErrUnknownTypeTemplate(
    "unknown type {}");
constexpr Error<const flat::TypeTemplate *> ErrMustBeAProtocol(
    "{} must be a protocol");
constexpr Error<const flat::TypeTemplate *> ErrCannotUseServicesInOtherDeclarations(
    "{} cannot use services in other declarations");
constexpr Error<const flat::TypeTemplate *> ErrCannotParametrizeTwice(
    "{} cannot parametrize twice");
constexpr Error<const flat::TypeTemplate *> ErrCannotBoundTwice(
    "{} cannot bound twice");
constexpr Error<const flat::TypeTemplate *> ErrCannotIndicateNullabilityTwice(
    "{} cannot indicate nullability twice");
constexpr Error<const flat::TypeTemplate *> ErrMustBeParameterized(
    "{} must be parametrized");
constexpr Error<const flat::TypeTemplate *> ErrMustHaveSize(
    "{} must have size");
constexpr Error<const flat::TypeTemplate *> ErrMustHaveNonZeroSize(
    "{} must have non-zero size");
constexpr Error<const flat::TypeTemplate *> ErrCannotBeParameterized(
    "{} cannot be parametrized");
constexpr Error<const flat::TypeTemplate *> ErrCannotHaveSize(
    "{} cannot have size");
constexpr Error<const flat::TypeTemplate *> ErrCannotBeNullable(
    "{} cannot be nullable");

constexpr Error<std::vector<std::string_view>, std::vector<std::string_view>,
                std::vector<std::string_view>> ErrUnusedImport(
    "Library {} imports {} but does not use it. Either use {}, or remove import.");

}  // namespace fidl

#endif  // ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_ERRORS_H_
