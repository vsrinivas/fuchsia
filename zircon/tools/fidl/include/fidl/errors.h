// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_ERRORS_H_
#define ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_ERRORS_H_

#include "error_types.h"

namespace fidl {

// ---------------------------------------------------------------------------
// Lexer
// ---------------------------------------------------------------------------
constexpr Error<std::string_view> ErrInvalidCharacter(
    "invalid character '{}'");

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------
constexpr Error ErrUnbalancedParseTree(
    "Internal compiler error: unbalanced parse tree");
constexpr Error ErrUnexpectedToken(
    "found unexpected token");
constexpr Error<Token::KindAndSubkind, Token::KindAndSubkind> ErrUnexpectedTokenOfKind(
    "unexpected token {}, was expecting {}");
constexpr Error<Token::KindAndSubkind, Token::KindAndSubkind> ErrUnexpectedIdentifier(
    "unexpected identifier {}, was expecting {}");
constexpr Error<std::string> ErrInvalidIdentifier(
    "invalid identifier '{}'");
constexpr Error<std::string> ErrInvalidLibraryNameComponent(
    "Invalid library name component {}");
constexpr Error<std::string> ErrDuplicateAttribute(
    "duplicate attribute with name '{}'");
constexpr Error ErrMissingOrdinalBeforeType(
    "missing ordinal before type");
constexpr Error ErrOrdinalOutOfBound(
    "ordinal out-of-bound");
constexpr Error ErrOrdinalsMustStartAtOne(
    "ordinals must start at 1");
constexpr Error ErrCompoundAliasIdentifier(
    "alias identifiers cannot contain '.'");
constexpr Error ErrBitsMustHaveOneMember(
    "must have at least one bits member");
constexpr Error ErrCannotAttachAttributesToCompose(
    "Cannot attach attributes to compose stanza");
constexpr Error ErrUnrecognizedProtocolMember(
    "unrecognized protocol member");
constexpr Error ErrExpectedProtocolMember(
    "expected protocol member");
constexpr Error ErrCannotAttachAttributesToReservedOrdinals(
    "Cannot attach attributes to reserved ordinals");
constexpr Error<Token::KindAndSubkind> ErrExpectedOrdinalOrCloseBrace(
    "Expected one of ordinal or '}', found {}");
constexpr Error ErrMustHaveNonReservedMember(
    "must have at least one non reserved member; you can use an empty struct to "
    "define a placeholder variant");
constexpr Error<Token::KindAndSubkind> ErrCannotSpecifyFlexible(
    "cannot specify flexible for {}");
constexpr Error<Token::KindAndSubkind> ErrCannotSpecifyStrict(
    "cannot specify strictness for {}");
constexpr Error ErrDocCommentOnParameters(
    "cannot have doc comment on parameters");
constexpr Error ErrXunionDeprecated(
    "xunion is deprecated, please use `flexible union` instead");
constexpr Error ErrStrictXunionDeprecated(
    "strict xunion is deprecated, please use `strict union` instead");
constexpr Error WarnCommentWithinDocCommentBlock(
    "cannot have comment within doc comment block");
constexpr Error WarnBlankLinesWithinDocCommentBlock(
    "cannot have blank lines within doc comment block");
constexpr Error WarnDocCommentMustBeFollowedByDeclaration(
    "doc comment must be followed by a declaration");
constexpr Error WarnLibraryImportsMustBeGroupedAtTopOfFile(
    "library imports must be grouped at top-of-file");

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
constexpr Error ErrBitsMemberMustBePowerOfTwo(
    "bits members must be powers of two");
constexpr Error<std::string, std::string, std::string, std::string>
ErrFlexibleEnumMemberWithMaxValue(
    "flexible enums must not have a member with a value of {}, which is "
    "reserved for the unknown value. either: remove the member with the {} "
    "value, change the member with the {} value to something other than {}, or "
    "explicitly specify the unknown value with the [Unknown] attribute. see "
    "<https://fuchsia.dev/fuchsia-src/development/languages/fidl/reference/"
    "language#unions> for more info.");
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
constexpr Error<std::string_view> ErrStructMemberMustBeSimple(
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
