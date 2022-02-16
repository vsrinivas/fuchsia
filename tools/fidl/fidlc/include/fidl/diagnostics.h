// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_DIAGNOSTICS_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_DIAGNOSTICS_H_

#include "diagnostic_types.h"

namespace fidl {

// ---------------------------------------------------------------------------
// Lexer
// ---------------------------------------------------------------------------
constexpr ErrorDef<std::string_view> ErrInvalidCharacter("invalid character '{}'");

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------
constexpr ErrorDef<std::string_view> ErrExpectedDeclaration("invalid declaration type {}");
constexpr ErrorDef ErrUnexpectedToken("found unexpected token");
constexpr ErrorDef<Token::KindAndSubkind, Token::KindAndSubkind> ErrUnexpectedTokenOfKind(
    "unexpected token {}, was expecting {}");
constexpr ErrorDef<Token::KindAndSubkind, Token::KindAndSubkind> ErrUnexpectedIdentifier(
    "unexpected identifier {}, was expecting {}");
constexpr ErrorDef<std::string_view> ErrInvalidIdentifier("invalid identifier '{}'");
constexpr ErrorDef<std::string_view> ErrInvalidLibraryNameComponent(
    "Invalid library name component {}");
constexpr ErrorDef ErrInvalidLayoutClass(
    "layouts must be of the class: bits, enum, struct, table, or union.");
constexpr ErrorDef ErrInvalidWrappedType("wrapped type for bits/enum must be an identifier");
constexpr ErrorDef ErrAttributeWithEmptyParens(
    "attributes without arguments must omit the trailing empty parentheses");
constexpr ErrorDef ErrAttributeArgsMustAllBeNamed(
    "attributes that take multiple arguments must name all of them explicitly");
constexpr ErrorDef ErrMissingOrdinalBeforeMember("missing ordinal before member");
constexpr ErrorDef ErrOrdinalOutOfBound("ordinal out-of-bound");
constexpr ErrorDef ErrOrdinalsMustStartAtOne("ordinals must start at 1");
constexpr ErrorDef ErrMustHaveOneMember("must have at least one member");
constexpr ErrorDef ErrUnrecognizedProtocolMember("unrecognized protocol member");
constexpr ErrorDef ErrExpectedProtocolMember("expected protocol member");
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
constexpr ErrorDef ErrLibraryImportsMustBeGroupedAtTopOfFile(
    "library imports must be grouped at top-of-file");
constexpr WarningDef WarnCommentWithinDocCommentBlock(
    "cannot have comment within doc comment block");
constexpr WarningDef WarnBlankLinesWithinDocCommentBlock(
    "cannot have blank lines within doc comment block");
constexpr WarningDef WarnDocCommentMustBeFollowedByDeclaration(
    "doc comment must be followed by a declaration");
constexpr ErrorDef ErrMustHaveOneProperty("must have at least one property");
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
constexpr ErrorDef<flat::Name, flat::Name, SourceSpan, std::string_view> ErrNameCollisionCanonical(
    "declaration name '{}' conflicts with '{}' from {}; both are represented "
    "by the canonical form '{}'");
constexpr ErrorDef<flat::Name> ErrDeclNameConflictsWithLibraryImport(
    "Declaration name '{}' conflicts with a library import. Consider using the "
    "'as' keyword to import the library under a different name.");
constexpr ErrorDef<flat::Name, std::string_view> ErrDeclNameConflictsWithLibraryImportCanonical(
    "Declaration name '{}' conflicts with a library import due to its "
    "canonical form '{}'. Consider using the 'as' keyword to import the "
    "library under a different name.");
constexpr ErrorDef ErrFilesDisagreeOnLibraryName(
    "Two files in the library disagree about the name of the library");
constexpr ErrorDef<std::vector<std::string_view>> ErrMultipleLibrariesWithSameName(
    "There are multiple libraries named '{}'");
constexpr ErrorDef<std::vector<std::string_view>> ErrDuplicateLibraryImport(
    "Library {} already imported. Did you require it twice?");
constexpr ErrorDef<std::vector<std::string_view>> ErrConflictingLibraryImport(
    "import of library '{}' conflicts with another library import");
constexpr ErrorDef<std::vector<std::string_view>, std::string_view>
    ErrConflictingLibraryImportAlias(
        "import of library '{}' under alias '{}' conflicts with another library import");
constexpr ErrorDef<const raw::AttributeList *> ErrAttributesNotAllowedOnLibraryImport(
    "no attributes allowed on library import, found: {}");
constexpr ErrorDef<std::vector<std::string_view>> ErrUnknownLibrary(
    "Could not find library named {}. Did you include its sources with --files?");
constexpr ErrorDef<SourceSpan> ErrProtocolComposedMultipleTimes(
    "protocol composed multiple times; previous was at {}");
constexpr ErrorDef ErrNullableTableMember("Table members cannot be nullable");
constexpr ErrorDef ErrNullableUnionMember("Union members cannot be nullable");

// ---------------------------------------------------------------------------
// Library::Compile: SortDeclarations
// ---------------------------------------------------------------------------
// ErrIncludeCycle is thrown either as part of SortDeclarations or as part of
// CompileStep, depending on the type of the cycle, because SortDeclarations
// understands the support for boxed recursive structs, while CompileStep
// handles recursive protocols and self-referencing type-aliases.
constexpr ErrorDef<std::vector<const flat::Decl *>> ErrIncludeCycle(
    "There is an includes-cycle in declarations: {}");

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
constexpr ErrorDef<flat::Name> ErrExpectedValueButGotType("{} is a type, but a value was expected");
constexpr ErrorDef<flat::Name, flat::Name> ErrMismatchedNameTypeAssignment(
    "mismatched named type assignment: cannot define a constant or default value of type {} "
    "using a value of type {}");
constexpr ErrorDef<const flat::Constant *, const flat::Type *, const flat::Type *>
    ErrTypeCannotBeConvertedToType("{} (type {}) cannot be converted to type {}");
constexpr ErrorDef<const flat::Constant *, const flat::Type *> ErrConstantOverflowsType(
    "{} overflows type {}");
constexpr ErrorDef ErrBitsMemberMustBePowerOfTwo("bits members must be powers of two");
constexpr ErrorDef<std::string_view> ErrFlexibleEnumMemberWithMaxValue(
    "flexible enums must not have a member with a value of {}, which is "
    "reserved for the unknown value. either: remove the member, change its "
    "value to something else, or explicitly specify the unknown value with "
    "the @unknown attribute. see "
    "<https://fuchsia.dev/fuchsia-src/reference/fidl/language/attributes#unknown> "
    "for more info.");
constexpr ErrorDef<const flat::Type *> ErrBitsTypeMustBeUnsignedIntegralPrimitive(
    "bits may only be of unsigned integral primitive type, found {}");
constexpr ErrorDef<const flat::Type *> ErrEnumTypeMustBeIntegralPrimitive(
    "enums may only be of integral primitive type, found {}");
constexpr ErrorDef ErrUnknownAttributeOnStrictEnumMember(
    "the @unknown attribute can be only be used on flexible enum members.");
constexpr ErrorDef ErrUnknownAttributeOnMultipleEnumMembers(
    "the @unknown attribute can be only applied to one enum member.");
constexpr ErrorDef ErrComposingNonProtocol("This declaration is not a protocol");
constexpr ErrorDef<const flat::Decl *> ErrInvalidParameterListType(
    "'{}' cannot be used as a parameter list");
constexpr ErrorDef<const flat::Decl *> ErrNotYetSupportedParameterListType(
    "'{}' cannot be yet be used as a parameter list (http://fxbug.dev/88343)");
constexpr ErrorDef<SourceSpan> ErrResponsesWithErrorsMustNotBeEmpty(
    "must define success type of method '{}'");
constexpr ErrorDef<std::string_view> ErrEmptyPayloadStructs(
    "method '{}' cannot have an empty struct as a payload, prefer omitting the payload altogether");
constexpr ErrorDef<std::string_view, SourceSpan> ErrDuplicateMethodName(
    "multiple protocol methods named '{}'; previous was at {}");
constexpr ErrorDef<std::string_view, std::string_view, SourceSpan, std::string_view>
    ErrDuplicateMethodNameCanonical(
        "protocol method '{}' conflicts with method '{}' from {}; both are "
        "represented by the canonical form '{}'");
constexpr ErrorDef ErrGeneratedZeroValueOrdinal("Ordinal value 0 disallowed.");
constexpr ErrorDef<SourceSpan, std::string_view> ErrDuplicateMethodOrdinal(
    "Multiple methods with the same ordinal in a protocol; previous was at {}. "
    "Consider using attribute @selector(\"{}\") to change the name used to "
    "calculate the ordinal.");
constexpr ErrorDef ErrInvalidSelectorValue(
    "invalid selector value, must be a method name or a fully qualified method name");
constexpr ErrorDef ErrFuchsiaIoExplicitOrdinals(
    "fuchsia.io must have explicit ordinals (https://fxbug.dev/77623)");
constexpr ErrorDef ErrPayloadStructHasDefaultMembers(
    "default values are not allowed on members of request/response structs");
constexpr ErrorDef<std::string_view, SourceSpan> ErrDuplicateServiceMemberName(
    "multiple service members named '{}'; previous was at {}");
constexpr ErrorDef<std::string_view, std::string_view, SourceSpan, std::string_view>
    ErrDuplicateServiceMemberNameCanonical(
        "service member '{}' conflicts with member '{}' from {}; both are "
        "represented by the canonical form '{}'");
constexpr ErrorDef ErrNullableServiceMember("service members cannot be nullable");
constexpr ErrorDef<std::string_view, SourceSpan> ErrDuplicateStructMemberName(
    "multiple struct fields named '{}'; previous was at {}");
constexpr ErrorDef<std::string_view, std::string_view, SourceSpan, std::string_view>
    ErrDuplicateStructMemberNameCanonical(
        "struct field '{}' conflicts with field '{}' from {}; both are represented "
        "by the canonical form '{}'");
constexpr ErrorDef<std::string_view, const flat::Type *> ErrInvalidStructMemberType(
    "struct field {} has an invalid default type {}");
constexpr ErrorDef ErrTooManyTableOrdinals(
    "table contains too many ordinals; tables are limited to 64 ordinals");
constexpr ErrorDef ErrMaxOrdinalNotTable(
    "the 64th ordinal of a table may only contain a table type");
constexpr ErrorDef<SourceSpan> ErrDuplicateTableFieldOrdinal(
    "multiple table fields with the same ordinal; previous was at {}");
constexpr ErrorDef<std::string_view, SourceSpan> ErrDuplicateTableFieldName(
    "multiple table fields named '{}'; previous was at {}");
constexpr ErrorDef<std::string_view, std::string_view, SourceSpan, std::string_view>
    ErrDuplicateTableFieldNameCanonical(
        "table field '{}' conflicts with field '{}' from {}; both are represented "
        "by the canonical form '{}'");
constexpr ErrorDef<SourceSpan> ErrDuplicateUnionMemberOrdinal(
    "multiple union fields with the same ordinal; previous was at {}");
constexpr ErrorDef<std::string_view, SourceSpan> ErrDuplicateUnionMemberName(
    "multiple union members named '{}'; previous was at {}");
constexpr ErrorDef<std::string_view, std::string_view, SourceSpan, std::string_view>
    ErrDuplicateUnionMemberNameCanonical(
        "union member '{}' conflicts with member '{}' from {}; both are represented "
        "by the canonical form '{}'");
constexpr ErrorDef<uint64_t> ErrNonDenseOrdinal(
    "missing ordinal {} (ordinals must be dense); consider marking it reserved");
constexpr ErrorDef ErrCouldNotParseSizeBound("unable to parse size bound");
constexpr ErrorDef<std::string_view> ErrCouldNotResolveMember("unable to resolve {} member");
constexpr ErrorDef<std::string_view> ErrCouldNotResolveMemberDefault(
    "unable to resolve {} default value");
constexpr ErrorDef ErrCouldNotResolveAttributeArg("unable to resolve attribute argument");
constexpr ErrorDef<std::string_view, std::string_view, SourceSpan> ErrDuplicateMemberName(
    "multiple {} members named '{}'; previous was at {}");
constexpr ErrorDef<std::string_view, std::string_view, std::string_view, SourceSpan,
                   std::string_view>
    ErrDuplicateMemberNameCanonical(
        "{} member '{}' conflicts with member '{}' from {}; both are "
        "represented by the canonical form '{}'");
constexpr ErrorDef<std::string_view, std::string_view, std::string_view, SourceSpan>
    ErrDuplicateMemberValue(
        "value of {} member '{}' conflicts with previously declared member '{}' at {}");
constexpr ErrorDef<std::string_view, SourceSpan> ErrDuplicateResourcePropertyName(
    "multiple resource properties named '{}'; previous was at {}");
constexpr ErrorDef<std::string_view, std::string_view, SourceSpan, std::string_view>
    ErrDuplicateResourcePropertyNameCanonical(
        "resource property '{}' conflicts with property '{}' from {}; both are "
        "represented by the canonical form '{}'");
constexpr ErrorDef<flat::Name, std::string_view, std::string_view, flat::Name>
    ErrTypeMustBeResource(
        "'{}' may contain handles (due to member '{}'), so it must "
        "be declared with the `resource` modifier: `resource {} {}`");
constexpr ErrorDef ErrInlineSizeExceeds64k(
    "inline objects greater than 64k not currently supported");
// TODO(fxbug.dev/70399): As part of consolidating name resolution, these should
// be grouped into a single "expected foo but got bar" error, along with
// ErrExpectedValueButGotType.
constexpr ErrorDef ErrCannotUseService("cannot use services in other declarations");
constexpr ErrorDef ErrCannotUseProtocol("cannot use protocol in this context");
constexpr ErrorDef ErrCannotUseType("cannot use type in this context");
constexpr ErrorDef ErrOnlyClientEndsInServices("service members must be client_end:P");
constexpr ErrorDef<types::Openness, flat::Name, types::Openness, flat::Name>
    ErrComposedProtocolTooOpen(
        "{} protocol '{}' cannot compose {} protocol '{}'; composed protocol may not be more open "
        "than composing protocol");
constexpr ErrorDef<types::Openness> ErrFlexibleTwoWayMethodRequiresOpenProtocol(
    "flexible two-way method may only be defined in an open protocol, not {}");
constexpr ErrorDef<std::string_view> ErrFlexibleOneWayMethodInClosedProtocol(
    "flexible {} may only be defined in an open or ajar protocol, not closed");

// ---------------------------------------------------------------------------
// Attribute Validation: Placement, Values, Constraints
// ---------------------------------------------------------------------------
constexpr ErrorDef<const flat::Attribute *> ErrInvalidAttributePlacement(
    "placement of attribute '{}' disallowed here");
constexpr ErrorDef<const flat::Attribute *> ErrDeprecatedAttribute("attribute '{}' is deprecated");
constexpr ErrorDef<std::string_view, SourceSpan> ErrDuplicateAttribute(
    "duplicate attribute '{}'; previous was at {}");
constexpr ErrorDef<std::string_view, std::string_view, SourceSpan, std::string_view>
    ErrDuplicateAttributeCanonical(
        "attribute '{}' conflicts with attribute '{}' from {}; both are "
        "represented by the canonical form '{}'");
constexpr ErrorDef<const flat::AttributeArg *, const flat::Attribute *> ErrCanOnlyUseStringOrBool(
    "argument '{}' on user-defined attribute '{}' cannot be a numeric "
    "value; use a bool or string instead");
constexpr ErrorDef ErrAttributeArgMustNotBeNamed(
    "attributes that take a single argument must not name that argument");
constexpr ErrorDef<const flat::AttributeArg *> ErrAttributeArgNotNamed(
    "attributes that take multiple arguments must name all of them explicitly, but '{}' was not");
constexpr ErrorDef<const flat::Attribute *, std::string_view> ErrMissingRequiredAttributeArg(
    "attribute '{}' is missing the required '{}' argument");
constexpr ErrorDef<const flat::Attribute *> ErrMissingRequiredAnonymousAttributeArg(
    "attribute '{}' is missing its required argument");
constexpr ErrorDef<const flat::Attribute *, std::string_view> ErrUnknownAttributeArg(
    "attribute '{}' does not support the '{}' argument");
constexpr ErrorDef<const flat::Attribute *, std::string_view, SourceSpan> ErrDuplicateAttributeArg(
    "attribute '{}' provides the '{}' argument multiple times; previous was at {}");
constexpr ErrorDef<const flat::Attribute *, std::string_view, std::string_view, SourceSpan,
                   std::string_view>
    ErrDuplicateAttributeArgCanonical(
        "attribute '{}' argument '{}' conflicts with argument '{}' from {}; both "
        "are represented by the canonical form '{}'");
constexpr ErrorDef<const flat::Attribute *> ErrAttributeDisallowsArgs(
    "attribute '{}' does not support arguments");
constexpr ErrorDef<std::string_view, const flat::Attribute *> ErrAttributeArgRequiresLiteral(
    "argument '{}' of attribute '{}' does not support referencing constants; "
    "please use a literal instead");
constexpr ErrorDef<const flat::Attribute *> ErrAttributeConstraintNotSatisfied(
    "declaration did not satisfy constraint of attribute '{}'");
constexpr ErrorDef<flat::Name> ErrUnionCannotBeSimple("union '{}' is not allowed to be simple");
constexpr ErrorDef<std::string_view> ErrMemberMustBeSimple("member '{}' is not simple");
constexpr ErrorDef<uint32_t, uint32_t> ErrTooManyBytes(
    "too large: only {} bytes allowed, but {} bytes found");
constexpr ErrorDef<uint32_t, uint32_t> ErrTooManyHandles(
    "too many handles: only {} allowed, but {} found");
constexpr ErrorDef ErrInvalidErrorType(
    "invalid error type: must be int32, uint32 or an enum thereof");
constexpr ErrorDef<std::string_view, std::set<std::string>> ErrInvalidTransportType(
    "invalid transport type: got {} expected one of {}");
constexpr ErrorDef<const flat::Attribute *, std::string_view> ErrBoundIsTooBig(
    "'{}' bound of '{}' is too big");
constexpr ErrorDef<const flat::Attribute *, std::string_view> ErrUnableToParseBound(
    "unable to parse '{}' bound of '{}'");
constexpr WarningDef<std::string_view, std::string_view> WarnAttributeTypo(
    "suspect attribute with name '{}'; did you mean '{}'?");
constexpr ErrorDef ErrInvalidGeneratedName("generated name must be a valid identifier");

// ---------------------------------------------------------------------------
// Type Templates
// ---------------------------------------------------------------------------
constexpr ErrorDef<flat::Name> ErrUnknownType("unknown type {}");
constexpr ErrorDef<const flat::TypeTemplate *> ErrCannotBeNullable("{} cannot be nullable");
constexpr ErrorDef<const flat::TypeTemplate *> ErrMustBeAProtocol("{} must be a protocol");
constexpr ErrorDef<const flat::TypeTemplate *> ErrCannotBoundTwice("{} cannot bound twice");
constexpr ErrorDef<const flat::TypeTemplate *> ErrCannotIndicateNullabilityTwice(
    "{} cannot indicate nullability twice");
constexpr ErrorDef<const flat::TypeTemplate *> ErrMustHaveNonZeroSize("{} must have non-zero size");
constexpr ErrorDef<const flat::TypeTemplate *, size_t, size_t> ErrWrongNumberOfLayoutParameters(
    "{} expected {} layout parameter(s), but got {}");
constexpr ErrorDef<const flat::TypeTemplate *, size_t, size_t> ErrTooManyConstraints(
    "{} expected at most {} constraints, but got {}");
constexpr ErrorDef ErrExpectedType("expected type but got a literal or constant");
constexpr ErrorDef<const flat::TypeTemplate *> ErrUnexpectedConstraint(
    "{} failed to resolve constraint");
// TODO(fxbug.dev/74193): Remove this error and allow re-constraining.
constexpr ErrorDef<const flat::TypeTemplate *> ErrCannotConstrainTwice(
    "{} cannot add additional constraint");
constexpr ErrorDef<const flat::TypeTemplate *> ErrProtocolConstraintRequired(
    "{} requires a protocol as its first constraint");
// The same error as ErrCannotBeNullable, but with a more specific message since the
// optionality of boxes may be confusing
constexpr ErrorDef ErrBoxCannotBeNullable(
    "cannot specify optionality for box, boxes are optional by default");
constexpr ErrorDef ErrBoxedTypeCannotBeNullable(
    "no double optionality, boxes are already optional");
constexpr ErrorDef<flat::Name> ErrCannotBeBoxed(
    "type {} cannot be boxed, try using optional instead");
constexpr ErrorDef<flat::Name> ErrResourceMustBeUint32Derived("resource {} must be uint32");
// TODO(fxbug.dev/75112): add these errors back by adding support in ResolveAs for
// storing errors
constexpr ErrorDef<flat::Name> ErrResourceMissingSubtypeProperty(
    "resource {} expected to have the subtype property, but it was missing");
constexpr ErrorDef<flat::Name> ErrResourceMissingRightsProperty(
    "resource {} expected to have the rights property, but it was missing");
constexpr ErrorDef<flat::Name> ErrResourceSubtypePropertyMustReferToEnum(
    "the subtype property must be an enum, but wasn't in resource {}");
constexpr ErrorDef ErrHandleSubtypeMustReferToResourceSubtype(
    "the subtype must be a constant referring to the resource's subtype enum");
constexpr ErrorDef<flat::Name> ErrResourceRightsPropertyMustReferToBits(
    "the rights property must be a bits, but wasn't in resource {}");
constexpr ErrorDef<std::vector<std::string_view>, std::vector<std::string_view>,
                   std::vector<std::string_view>>
    ErrUnusedImport("Library {} imports {} but does not use it. Either use {}, or remove import.");

}  // namespace fidl

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_DIAGNOSTICS_H_
