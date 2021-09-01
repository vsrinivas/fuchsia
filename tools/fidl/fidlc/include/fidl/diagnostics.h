// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_DIAGNOSTICS_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_DIAGNOSTICS_H_

#include "diagnostic_types.h"

namespace fidl {
namespace diagnostics {

// ---------------------------------------------------------------------------
// Lexer
// ---------------------------------------------------------------------------
constexpr ErrorDef<std::string_view> ErrInvalidCharacter("invalid character '{}'");

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------
constexpr ErrorDef<std::basic_string_view<char>> ErrExpectedDeclaration(
    "invalid declaration type {}");
constexpr ErrorDef ErrUnexpectedToken("found unexpected token");
constexpr ErrorDef<Token::KindAndSubkind, Token::KindAndSubkind> ErrUnexpectedTokenOfKind(
    "unexpected token {}, was expecting {}");
constexpr ErrorDef<Token::KindAndSubkind, Token::KindAndSubkind> ErrUnexpectedIdentifier(
    "unexpected identifier {}, was expecting {}");
constexpr ErrorDef<std::string> ErrInvalidIdentifier("invalid identifier '{}'");
constexpr ErrorDef<std::string> ErrInvalidLibraryNameComponent("Invalid library name component {}");
constexpr ErrorDef<std::string> ErrDuplicateAttribute("duplicate attribute with name '{}'");

// start new_syntax
// TODO(fxbug.dev/70247): remove when new syntax fully implemented.
constexpr ErrorDef ErrMisplacedSyntaxVersion(
    "syntax declaration must be at the top of the file, preceding the library declaration");
constexpr ErrorDef ErrRemoveSyntaxVersion(
    "the deprecated_syntax token is only recognized when the experimental allow_new_syntax flag is "
    "enabled");
constexpr ErrorDef ErrEmptyConstraints("no constraints specified");
constexpr ErrorDef ErrInvalidLayoutClass(
    "layouts must be of the class: bits, enum, struct, table, or union.");
constexpr ErrorDef ErrInvalidWrappedType("wrapped type for bits/enum must be an identifier");
constexpr ErrorDef ErrEmptyLayoutParameterList("no layout parameters specified");
// end new_syntax

// TODO(fxbug.dev/65978): This is a misnomer in the new syntax: the ordinal comes
// before the member name, not the type.
constexpr ErrorDef ErrMissingOrdinalBeforeType("missing ordinal before type");
constexpr ErrorDef ErrOrdinalOutOfBound("ordinal out-of-bound");
constexpr ErrorDef ErrOrdinalsMustStartAtOne("ordinals must start at 1");
constexpr ErrorDef ErrCompoundAliasIdentifier("alias identifiers cannot contain '.'");
constexpr ErrorDef ErrOldUsingSyntaxDeprecated(
    "old `using Name = Type;` syntax is disallowed; use `alias Name = Type;` instead");
constexpr ErrorDef ErrMustHaveOneMember("must have at least one member");
constexpr ErrorDef ErrUnrecognizedProtocolMember("unrecognized protocol member");
constexpr ErrorDef ErrExpectedProtocolMember("expected protocol member");
constexpr ErrorDef ErrCannotAttachAttributesToReservedOrdinals(
    "Cannot attach attributes to reserved ordinals");
constexpr ErrorDef ErrCannotAttachAttributeToIdentifier("cannot attach attributes to identifiers");
constexpr ErrorDef ErrRedundantAttributePlacement(
    "cannot specify attributes on the type declaration and the corresponding layout at the same "
    "time; please merge them into one location instead");
constexpr ErrorDef<Token::KindAndSubkind> ErrExpectedOrdinalOrCloseBrace(
    "Expected one of ordinal or '}', found {}");
constexpr ErrorDef ErrMustHaveNonReservedMember(
    "must have at least one non reserved member; you can use an empty struct to "
    "define a placeholder variant");
constexpr ErrorDef ErrDocCommentOnParameters("cannot have doc comment on parameters");
constexpr ErrorDef ErrXunionDeprecated("xunion is deprecated, please use `flexible union` instead");
constexpr ErrorDef ErrStrictXunionDeprecated(
    "strict xunion is deprecated, please use `strict union` instead");
constexpr ErrorDef ErrLibraryImportsMustBeGroupedAtTopOfFile(
    "library imports must be grouped at top-of-file");
constexpr WarningDef WarnCommentWithinDocCommentBlock(
    "cannot have comment within doc comment block");
constexpr WarningDef WarnBlankLinesWithinDocCommentBlock(
    "cannot have blank lines within doc comment block");
constexpr WarningDef WarnDocCommentMustBeFollowedByDeclaration(
    "doc comment must be followed by a declaration");
constexpr ErrorDef ErrMustHaveOneProperty("must have at least one property");
constexpr ErrorDef ErrOldHandleSyntax(
    "handle<type> is no longer supported, please use zx.handle:TYPE");
constexpr ErrorDef<Token::KindAndSubkind, Token::KindAndSubkind> ErrCannotSpecifyModifier(
    "cannot specify modifier {} for {}");
constexpr ErrorDef<Token::KindAndSubkind> ErrCannotSpecifySubtype("cannot specify subtype for {}");
constexpr ErrorDef<Token::KindAndSubkind> ErrDuplicateModifier(
    "duplicate occurrence of modifier {}");
constexpr ErrorDef<Token::KindAndSubkind, Token::KindAndSubkind> ErrConflictingModifier(
    "modifier {} conflicts with modifier {}");

// ---------------------------------------------------------------------------
// Library::ConsumeFile: Consume* methods and declaration registration
// ---------------------------------------------------------------------------
constexpr ErrorDef<flat::Name, SourceSpan> ErrNameCollision(
    "multiple declarations of '{}'; also declared at {}");
constexpr ErrorDef<flat::Name, flat::Name, SourceSpan, std::string> ErrNameCollisionCanonical(
    "declaration name '{}' conflicts with '{}' from {}; both are represented "
    "by the canonical form '{}'");
constexpr ErrorDef<flat::Name> ErrDeclNameConflictsWithLibraryImport(
    "Declaration name '{}' conflicts with a library import. Consider using the "
    "'as' keyword to import the library under a different name.");
constexpr ErrorDef<flat::Name, std::string> ErrDeclNameConflictsWithLibraryImportCanonical(
    "Declaration name '{}' conflicts with a library import due to its "
    "canonical form '{}'. Consider using the 'as' keyword to import the "
    "library under a different name.");
constexpr ErrorDef ErrFilesDisagreeOnLibraryName(
    "Two files in the library disagree about the name of the library");
constexpr ErrorDef<std::vector<std::string_view>> ErrDuplicateLibraryImport(
    "Library {} already imported. Did you require it twice?");
constexpr ErrorDef<raw::AttributeListOld *> ErrAttributesOldNotAllowedOnLibraryImport(
    "no attributes allowed on library import, found: {}");
constexpr ErrorDef<raw::AttributeListNew *> ErrAttributesNewNotAllowedOnLibraryImport(
    "no attributes allowed on library import, found: {}");
constexpr ErrorDef<std::vector<std::string_view>> ErrUnknownLibrary(
    "Could not find library named {}. Did you include its sources with --files?");
constexpr ErrorDef ErrProtocolComposedMultipleTimes("protocol composed multiple times");
constexpr ErrorDef ErrDefaultsOnTablesNotSupported("Defaults on table members are not supported.");
constexpr ErrorDef ErrDefaultsOnUnionsNotSupported("Defaults on union members are not supported.");
constexpr ErrorDef ErrNullableTableMember("Table members cannot be nullable");
constexpr ErrorDef ErrNullableUnionMember("Union members cannot be nullable");

// ---------------------------------------------------------------------------
// Library::Compile: SortDeclarations
// ---------------------------------------------------------------------------
// NOTE: currently, neither of these errors will actually be thrown as part of SortDeclarations,
// since they will be caught earlier during the compilation process. Specifically,
// ErrFailedConstantLookup will never be thrown (ErrCannotResolveConstantValue is caught first),
// and ErrIncludeCycle is thrown as part of the compilation step rather than here.
// We still keep these errors so that SortDeclarations can work "standalone" and does not depend
// on whether compilation occurs first. This makes it easier to move/reorder later if needed
constexpr ErrorDef<flat::Name> ErrFailedConstantLookup("Unable to find the constant named: {}");
constexpr ErrorDef ErrIncludeCycle("There is an includes-cycle in declarations");

// ---------------------------------------------------------------------------
// Library::Compile: Compilation, Resolution, Validation
// ---------------------------------------------------------------------------
constexpr ErrorDef<flat::Name> ErrAnonymousNameReference("cannot refer to anonymous name {}");
constexpr ErrorDef<std::vector<std::string_view>, std::vector<std::string_view>>
    ErrUnknownDependentLibrary(
        "Unknown dependent library {} or reference to member of "
        "library {}. Did you require it with `using`?");
constexpr ErrorDef<const flat::Type *> ErrInvalidConstantType("invalid constant type {}");
constexpr ErrorDef ErrCannotResolveConstantValue("unable to resolve constant value");
constexpr ErrorDef ErrOrOperatorOnNonPrimitiveValue(
    "Or operator can only be applied to primitive-kinded values");
constexpr ErrorDef<std::string_view> ErrUnknownEnumMember("unknown enum member '{}'");
constexpr ErrorDef<std::string_view> ErrUnknownBitsMember("unknown bits member '{}'");
constexpr ErrorDef<flat::Name, std::string_view> ErrNewTypesNotAllowed(
    "newtypes not allowed: type declaration {} defines a new type of the existing {} type, which "
    "is not yet supported");
constexpr ErrorDef<> ErrAnonymousTypesNotAllowed(
    "anonymous layouts are not yet supported: layouts must be specified in a `type MyLayout = ...` "
    "layout introduction statement.");
constexpr ErrorDef<flat::Name> ErrExpectedValueButGotType("{} is a type, but a value was expected");
constexpr ErrorDef<flat::Name, flat::Name> ErrMismatchedNameTypeAssignment(
    "mismatched named type assignment: cannot define a constant or default value of type {} "
    "using a value of type {}");
constexpr ErrorDef<flat::IdentifierConstant *, const flat::Type *, const flat::Type *>
    ErrCannotConvertConstantToType("{}, of type {}, cannot be converted to type {}");
constexpr ErrorDef<flat::LiteralConstant *, uint64_t, const flat::Type *>
    ErrStringConstantExceedsSizeBound("{} (string:{}) exceeds the size bound of type {}");
constexpr ErrorDef<flat::LiteralConstant *, const flat::Type *>
    ErrConstantCannotBeInterpretedAsType("{} cannot be interpreted as type {}");
constexpr ErrorDef ErrCouldNotResolveIdentifierToType("could not resolve identifier to a type");
constexpr ErrorDef ErrBitsMemberMustBePowerOfTwo("bits members must be powers of two");
constexpr ErrorDef<std::string, std::string, std::string, std::string>
    ErrFlexibleEnumMemberWithMaxValue(
        "flexible enums must not have a member with a value of {}, which is "
        "reserved for the unknown value. either: remove the member with the {} "
        "value, change the member with the {} value to something other than {}, or "
        "explicitly specify the unknown value with the [Unknown] attribute. see "
        "<https://fuchsia.dev/fuchsia-src/development/languages/fidl/reference/"
        "language#unions> for more info.");
// TODO(65978): Investigate folding these two errors into InvalidWrappedType
//  when removing old syntax.
constexpr ErrorDef<const flat::Type *> ErrBitsTypeMustBeUnsignedIntegralPrimitive(
    "bits may only be of unsigned integral primitive type, found {}");
constexpr ErrorDef<const flat::Type *> ErrEnumTypeMustBeIntegralPrimitive(
    "enums may only be of integral primitive type, found {}");
constexpr ErrorDef ErrUnknownAttributeOnInvalidType(
    "[Unknown] attribute can be only be used on flexible or [Transitional] types.");
constexpr ErrorDef ErrUnknownAttributeOnMultipleMembers(
    "[Unknown] attribute can be only applied to one member.");
constexpr ErrorDef ErrComposingNonProtocol("This declaration is not a protocol");
constexpr ErrorDef ErrNamedParameterListTypesNotYetSupported(
    "using named types in this position is not yet allowed, use `struct { ... }` instead "
    "(http://fxbug.dev/76349)");
constexpr ErrorDef<flat::Decl *> ErrInvalidParameterListType(
    "'{}' cannot be used as a parameter list");
constexpr ErrorDef<flat::Decl *> ErrNotYetSupportedParameterListType(
    "'{}' cannot be yet be used as a parameter list (http://fxbug.dev/76349)");
constexpr ErrorDef<SourceSpan> ErrResponsesWithErrorsMustNotBeEmpty(
    "must define success type of method '{}'");
constexpr ErrorDef<flat::Name> ErrEmptyPayloadStructs(
    "method '{}' cannot have an empty struct as a payload, prefer omitting the payload altogether");
constexpr ErrorDef<flat::Name> ErrNotYetSupportedAttributesOnPayloadStructs(
    "method '{}' cannot yet have attributes on its payload type (http://fxbug.dev/74955)");
constexpr ErrorDef<std::string_view, SourceSpan> ErrDuplicateMethodName(
    "multiple protocol methods named '{}'; previous was at {}");
constexpr ErrorDef<std::string_view, std::string_view, SourceSpan, std::string>
    ErrDuplicateMethodNameCanonical(
        "protocol method '{}' conflicts with method '{}' from {}; both are "
        "represented by the canonical form '{}'");
constexpr ErrorDef ErrGeneratedZeroValueOrdinal("Ordinal value 0 disallowed.");
constexpr ErrorDef<SourceSpan, std::string> ErrDuplicateMethodOrdinal(
    "Multiple methods with the same ordinal in a protocol; previous was at {}. "
    "Consider using attribute [Selector=\"{}\"] to change the name used to "
    "calculate the ordinal.");
constexpr ErrorDef ErrInvalidSelectorValue(
    "invalid selector value, must be a method name or a fully qualified method name");
constexpr ErrorDef<std::string_view, SourceSpan> ErrDuplicateMethodParameterName(
    "multiple method parameters named '{}'; previous was at {}");
constexpr ErrorDef<std::string_view, std::string_view, SourceSpan, std::string>
    ErrDuplicateMethodParameterNameCanonical(
        "method parameter '{}' conflicts with parameter '{}' from {}; both are "
        "represented by the canonical form '{}'s");
constexpr ErrorDef<std::string_view, SourceSpan> ErrDuplicateServiceMemberName(
    "multiple service members named '{}'; previous was at {}");
constexpr ErrorDef<std::string_view, std::string_view, SourceSpan, std::string>
    ErrDuplicateServiceMemberNameCanonical(
        "service member '{}' conflicts with member '{}' from {}; both are "
        "represented by the canonical form '{}'");
constexpr ErrorDef ErrNullableServiceMember("service members cannot be nullable");
constexpr ErrorDef<std::string_view, SourceSpan> ErrDuplicateStructMemberName(
    "multiple struct fields named '{}'; previous was at {}");
constexpr ErrorDef<std::string_view, std::string_view, SourceSpan, std::string>
    ErrDuplicateStructMemberNameCanonical(
        "struct field '{}' conflicts with field '{}' from {}; both are represented "
        "by the canonical form '{}'");
constexpr ErrorDef<std::string, const flat::Type *> ErrInvalidStructMemberType(
    "struct field {} has an invalid default type{}");
constexpr ErrorDef<SourceSpan> ErrDuplicateTableFieldOrdinal(
    "multiple table fields with the same ordinal; previous was at {}");
constexpr ErrorDef<std::string_view, SourceSpan> ErrDuplicateTableFieldName(
    "multiple table fields named '{}'; previous was at {}");
constexpr ErrorDef<std::string_view, std::string_view, SourceSpan, std::string>
    ErrDuplicateTableFieldNameCanonical(
        "table field '{}' conflicts with field '{}' from {}; both are represented "
        "by the canonical form '{}'");
constexpr ErrorDef<SourceSpan> ErrDuplicateUnionMemberOrdinal(
    "multiple union fields with the same ordinal; previous was at {}");
constexpr ErrorDef<std::string_view, SourceSpan> ErrDuplicateUnionMemberName(
    "multiple union members named '{}'; previous was at {}");
constexpr ErrorDef<std::string_view, std::string_view, SourceSpan, std::string>
    ErrDuplicateUnionMemberNameCanonical(
        "union member '{}' conflicts with member '{}' from {}; both are represented "
        "by the canonical form '{}'");
constexpr ErrorDef<uint64_t> ErrNonDenseOrdinal(
    "missing ordinal {} (ordinals must be dense); consider marking it reserved");
constexpr ErrorDef ErrCouldNotResolveHandleRights("unable to resolve handle rights");
constexpr ErrorDef<flat::Name> ErrCouldNotResolveHandleSubtype(
    "unable to resolve handle subtype {}");
constexpr ErrorDef ErrCouldNotParseSizeBound("unable to parse size bound");
constexpr ErrorDef<std::string> ErrCouldNotResolveMember("unable to resolve {} member");
constexpr ErrorDef<std::string_view, std::string_view, SourceSpan> ErrDuplicateMemberName(
    "multiple {} members named '{}'; previous was at {}");
constexpr ErrorDef<std::string_view, std::string_view, std::string_view, SourceSpan, std::string>
    ErrDuplicateMemberNameCanonical(
        "{} member '{}' conflicts with member '{}' from {}; both are "
        "represented by the canonical form '{}'");
constexpr ErrorDef<std::string_view, std::string_view, std::string_view, SourceSpan>
    ErrDuplicateMemberValue(
        "value of {} member '{}' conflicts with previously declared member '{}' at {}");
constexpr ErrorDef<SourceSpan> ErrDuplicateResourcePropertyName(
    "multiple resource properties with the same name; previous was at {}");
constexpr ErrorDef<flat::Name, std::string_view, std::string_view, flat::Name>
    ErrTypeMustBeResource(
        "'{}' may contain handles (due to member '{}'), so it must "
        "be declared with the `resource` modifier: `resource {} {}`");
constexpr ErrorDef ErrInlineSizeExceeds64k(
    "inline objects greater than 64k not currently supported");
// TODO(fxbug.dev/70399): As part of consolidating name resolution, these should
// be grouped into a single "expected foo but got bar" error, along with
// ErrExpectedValueButGotType.
constexpr ErrorDef<> ErrCannotUseService("cannot use services in other declarations");
constexpr ErrorDef<> ErrCannotUseProtocol("cannot use protocol in this context");
constexpr ErrorDef<> ErrCannotUseType("cannot use type in this context");
constexpr ErrorDef<> ErrMustBeTransportSide(
    "service members must be of type `client_end` or `server_end`");

// ---------------------------------------------------------------------------
// Attribute Validation: Placement, Values, Constraints
// ---------------------------------------------------------------------------
constexpr ErrorDef<flat::Attribute *> ErrInvalidAttributePlacement(
    "placement of attribute '{}' disallowed here");
constexpr ErrorDef<flat::Attribute *> ErrDeprecatedAttribute("attribute '{}' is deprecated");
constexpr ErrorDef<flat::Attribute *, std::string, std::set<std::string>> ErrInvalidAttributeValue(
    "attribute '{}' has invalid value '{}', should be one of '{}'");
constexpr ErrorDef<flat::Attribute *, std::string> ErrAttributeConstraintNotSatisfied(
    "declaration did not satisfy constraint of attribute '{}' with value '{}'");
constexpr ErrorDef<flat::Name> ErrUnionCannotBeSimple("union '{}' is not allowed to be simple");
constexpr ErrorDef<std::string_view> ErrMemberMustBeSimple("member '{}' is not simple");
constexpr ErrorDef<uint32_t, uint32_t> ErrTooManyBytes(
    "too large: only {} bytes allowed, but {} bytes found");
constexpr ErrorDef<uint32_t, uint32_t> ErrTooManyHandles(
    "too many handles: only {} allowed, but {} found");
constexpr ErrorDef ErrInvalidErrorType(
    "invalid error type: must be int32, uint32 or an enum therof");
constexpr ErrorDef<std::string, std::set<std::string>> ErrInvalidTransportType(
    "invalid transport type: got {} expected one of {}");
constexpr ErrorDef<flat::Attribute *> ErrInvalidAttributeType("attribute '{}' has an invalid type");
constexpr ErrorDef<flat::Attribute *, std::string> ErrBoundIsTooBig(
    "'{}' bound of '{}' is too big");
constexpr ErrorDef<flat::Attribute *, std::string> ErrUnableToParseBound(
    "unable to parse '{}' bound of '{}'");
constexpr WarningDef<std::string, std::string> WarnAttributeTypo(
    "suspect attribute with name '{}'; did you mean '{}'?");
constexpr ErrorDef<flat::Attribute *> ErrEmptyAttributeArg("attribute '{}' requires an argument");
constexpr ErrorDef<> ErrInvalidNameOverride("name override must be a valid identifier");

// ---------------------------------------------------------------------------
// Type Templates
// ---------------------------------------------------------------------------
constexpr ErrorDef<flat::Name> ErrUnknownType("unknown type {}");
constexpr ErrorDef<const flat::TypeTemplate *> ErrCannotBeNullable("{} cannot be nullable");

// old style
constexpr ErrorDef<const flat::TypeTemplate *> ErrMustBeAProtocol("{} must be a protocol");
constexpr ErrorDef<const flat::TypeTemplate *> ErrCannotParameterizeAlias(
    "{}: aliases cannot be parameterized");
constexpr ErrorDef<const flat::TypeTemplate *> ErrCannotBoundTwice("{} cannot bound twice");
constexpr ErrorDef<const flat::TypeTemplate *> ErrCannotIndicateNullabilityTwice(
    "{} cannot indicate nullability twice");
constexpr ErrorDef<const flat::TypeTemplate *> ErrMustBeParameterized("{} must be parametrized");
constexpr ErrorDef<const flat::TypeTemplate *> ErrMustHaveSize("{} must have size");
constexpr ErrorDef<const flat::TypeTemplate *> ErrMustHaveNonZeroSize("{} must have non-zero size");
constexpr ErrorDef<const flat::TypeTemplate *> ErrCannotBeParameterized(
    "{} cannot be parametrized");
constexpr ErrorDef<const flat::TypeTemplate *> ErrCannotHaveSize("{} cannot have size");

// new style
constexpr ErrorDef<const flat::TypeTemplate *, size_t, size_t> ErrWrongNumberOfLayoutParameters(
    "{} expected {} layout parameter(s), but got {}");
constexpr ErrorDef<const flat::TypeTemplate *, size_t, size_t> ErrTooManyConstraints(
    "{} expected at most {} constraints, but got {}");
constexpr ErrorDef<> ErrExpectedType("expected type but got a literal or constant");
constexpr ErrorDef<const flat::TypeTemplate *> ErrUnexpectedConstraint(
    "{} failed to resolve constraint");
// TODO(fxbug.dev/74193): Remove this error and allow re-constraining.
constexpr ErrorDef<const flat::TypeTemplate *> ErrCannotConstrainTwice(
    "{} cannot add additional constraint");
constexpr ErrorDef<const flat::TypeTemplate *> ErrProtocolConstraintRequired(
    "{} requires a protocol as its first constraint");
// The same error as ErrCannotBeNullable, but with a more specific message since the
// optionality of boxes may be confusing
constexpr ErrorDef<> ErrBoxCannotBeNullable(
    "cannot specify optionality for box, boxes are optional by default");
constexpr ErrorDef<> ErrBoxedTypeCannotBeNullable(
    "no double optionality, boxes are already optional");
constexpr ErrorDef<flat::Name> ErrCannotBeBoxed(
    "type {} cannot be boxed, try using optional instead");

// other
// TODO(fxbug.dev/764629): when we allow non-handle resources, this will become just ErrNotResource
constexpr ErrorDef<flat::Name> ErrHandleNotResource("{} is not a defined resource");
constexpr ErrorDef<flat::Name> ErrResourceMustBeUint32Derived("resource {} must be uint32");
// TODO(fxbug.dev/75112): add these errors back by adding support in ResolveAs for
// storing errors
constexpr ErrorDef<flat::Name> ErrResourceMissingSubtypeProperty(
    "resource {} expected to have the subtype property, but it was missing");
constexpr ErrorDef<flat::Name> ErrResourceMissingRightsProperty(
    "resource {} expected to have the rights property, but it was missing");
constexpr ErrorDef<flat::Name> ErrResourceSubtypePropertyMustReferToEnum(
    "the subtype property must be an enum, but wasn't in resource {}");
constexpr ErrorDef<> ErrHandleSubtypeMustReferToResourceSubtype(
    "the subtype must be a constant referring to the resource's subtype enum");
constexpr ErrorDef<flat::Name> ErrResourceRightsPropertyMustReferToBits(
    "the rights property must be a bits, but wasn't in resource {}");

constexpr ErrorDef<std::vector<std::string_view>, std::vector<std::string_view>,
                   std::vector<std::string_view>>
    ErrUnusedImport("Library {} imports {} but does not use it. Either use {}, or remove import.");

}  // namespace diagnostics
}  // namespace fidl

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_DIAGNOSTICS_H_
