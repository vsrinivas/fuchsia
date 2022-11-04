// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_DIAGNOSTICS_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_DIAGNOSTICS_H_

#include "tools/fidl/fidlc/include/fidl/diagnostic_types.h"
#include "tools/fidl/fidlc/include/fidl/source_span.h"
#include "tools/fidl/fidlc/include/fidl/versioning_types.h"

namespace fidl {

constexpr RetiredDef<0> ErrAlwaysRetired("error id fi-0000 was always retired");
constexpr ErrorDef<1, std::string_view> ErrInvalidCharacter("invalid character '{}'");
constexpr ErrorDef<2> ErrUnexpectedLineBreak("unexpected line-break in string literal");
constexpr ErrorDef<3, std::string_view> ErrInvalidEscapeSequence("invalid escape sequence '{}'");
constexpr ErrorDef<4, char> ErrInvalidHexDigit("invalid hex digit '{}'");
constexpr RetiredDef<5, char> ErrInvalidOctDigit("invalid oct digit '{}'");
constexpr ErrorDef<6, std::string_view> ErrExpectedDeclaration("invalid declaration type {}");
constexpr ErrorDef<7> ErrUnexpectedToken("found unexpected token");
constexpr ErrorDef<8, Token::KindAndSubkind, Token::KindAndSubkind> ErrUnexpectedTokenOfKind(
    "unexpected token {}, was expecting {}");
constexpr ErrorDef<9, Token::KindAndSubkind, Token::KindAndSubkind> ErrUnexpectedIdentifier(
    "unexpected identifier {}, was expecting {}");
constexpr ErrorDef<10, std::string_view> ErrInvalidIdentifier("invalid identifier '{}'");
constexpr ErrorDef<11, std::string_view> ErrInvalidLibraryNameComponent(
    "Invalid library name component {}");
constexpr ErrorDef<12> ErrInvalidLayoutClass(
    "layouts must be of the class: bits, enum, struct, table, or union.");
constexpr ErrorDef<13> ErrInvalidWrappedType("wrapped type for bits/enum must be an identifier");
constexpr ErrorDef<14> ErrAttributeWithEmptyParens(
    "attributes without arguments must omit the trailing empty parentheses");
constexpr ErrorDef<15> ErrAttributeArgsMustAllBeNamed(
    "attributes that take multiple arguments must name all of them explicitly");
constexpr ErrorDef<16> ErrMissingOrdinalBeforeMember("missing ordinal before member");
constexpr ErrorDef<17> ErrOrdinalOutOfBound("ordinal out-of-bound");
constexpr ErrorDef<18> ErrOrdinalsMustStartAtOne("ordinals must start at 1");
constexpr ErrorDef<19> ErrMustHaveOneMember("must have at least one member");
constexpr ErrorDef<20> ErrInvalidProtocolMember("invalid protocol member");
constexpr RetiredDef<21> ErrExpectedProtocolMember(
    "merged ErrUnrecognizedProtocolMember (fi-0020) and ErrExpectedProtocolMember (fi-0021) into "
    "fi-0020 as ErrInvalidProtocolMember");
constexpr ErrorDef<22> ErrCannotAttachAttributeToIdentifier(
    "cannot attach attributes to identifiers");
constexpr ErrorDef<23> ErrRedundantAttributePlacement(
    "cannot specify attributes on the type declaration and the corresponding layout at the same "
    "time; please merge them into one location instead");
constexpr ErrorDef<24> ErrDocCommentOnParameters("cannot have doc comment on parameters");
constexpr ErrorDef<25> ErrLibraryImportsMustBeGroupedAtTopOfFile(
    "library imports must be grouped at top-of-file");
constexpr WarningDef<26> WarnCommentWithinDocCommentBlock(
    "cannot have comment within doc comment block");
constexpr WarningDef<27> WarnBlankLinesWithinDocCommentBlock(
    "cannot have blank lines within doc comment block");
constexpr WarningDef<28> WarnDocCommentMustBeFollowedByDeclaration(
    "doc comment must be followed by a declaration");
constexpr ErrorDef<29> ErrMustHaveOneProperty("must have at least one property");
constexpr ErrorDef<30, Token::KindAndSubkind, Token::KindAndSubkind> ErrCannotSpecifyModifier(
    "cannot specify modifier {} for {}");
constexpr ErrorDef<31, Token::KindAndSubkind> ErrCannotSpecifySubtype(
    "cannot specify subtype for {}");
constexpr ErrorDef<32, Token::KindAndSubkind> ErrDuplicateModifier(
    "duplicate occurrence of modifier {}");
constexpr ErrorDef<33, Token::KindAndSubkind, Token::KindAndSubkind> ErrConflictingModifier(
    "modifier {} conflicts with modifier {}");
constexpr ErrorDef<34, std::string_view, SourceSpan> ErrNameCollision(
    "the name '{}' conflicts with another declaration at {}");
constexpr ErrorDef<35, std::string_view, std::string_view, SourceSpan, std::string_view>
    ErrNameCollisionCanonical(
        "the name '{}' conflicts with '{}' from {}; both are represented by "
        "the canonical form '{}'");
constexpr ErrorDef<36, std::string_view, SourceSpan, VersionSet, Platform> ErrNameOverlap(
    "the name '{}' conflicts with another declaration at {}; both are "
    "available {} of platform '{}'");
constexpr ErrorDef<37, std::string_view, std::string_view, SourceSpan, std::string_view, VersionSet,
                   Platform>
    ErrNameOverlapCanonical(
        "the name '{}' conflicts with '{}' from {}; both are represented "
        "by the canonical form '{}' and are available {} of platform '{}'");
constexpr ErrorDef<38, flat::Name> ErrDeclNameConflictsWithLibraryImport(
    "Declaration name '{}' conflicts with a library import. Consider using the "
    "'as' keyword to import the library under a different name.");
constexpr ErrorDef<39, flat::Name, std::string_view> ErrDeclNameConflictsWithLibraryImportCanonical(
    "Declaration name '{}' conflicts with a library import due to its "
    "canonical form '{}'. Consider using the 'as' keyword to import the "
    "library under a different name.");
constexpr ErrorDef<40> ErrFilesDisagreeOnLibraryName(
    "Two files in the library disagree about the name of the library");
constexpr ErrorDef<41, std::vector<std::string_view>> ErrMultipleLibrariesWithSameName(
    "There are multiple libraries named '{}'");
constexpr ErrorDef<42, std::vector<std::string_view>> ErrDuplicateLibraryImport(
    "Library {} already imported. Did you require it twice?");
constexpr ErrorDef<43, std::vector<std::string_view>> ErrConflictingLibraryImport(
    "import of library '{}' conflicts with another library import");
constexpr ErrorDef<44, std::vector<std::string_view>, std::string_view>
    ErrConflictingLibraryImportAlias(
        "import of library '{}' under alias '{}' conflicts with another library import");
constexpr ErrorDef<45, const raw::AttributeList *> ErrAttributesNotAllowedOnLibraryImport(
    "no attributes allowed on library import, found: {}");
constexpr ErrorDef<46, std::vector<std::string_view>> ErrUnknownLibrary(
    "Could not find library named {}. Did you include its sources with --files?");
constexpr ErrorDef<47, SourceSpan> ErrProtocolComposedMultipleTimes(
    "protocol composed multiple times; previous was at {}");
constexpr ErrorDef<48> ErrOptionalTableMember("Table members cannot be optional");
constexpr ErrorDef<49> ErrOptionalUnionMember("Union members cannot be optional");
constexpr ErrorDef<50> ErrDeprecatedStructDefaults(
    "Struct defaults are deprecated and should not be used (see RFC-0160)");
constexpr ErrorDef<51, std::vector<std::string_view>, std::vector<std::string_view>>
    ErrUnknownDependentLibrary(
        "Unknown dependent library {} or reference to member of "
        "library {}. Did you require it with `using`?");
constexpr ErrorDef<52, std::string_view, std::vector<std::string_view>> ErrNameNotFound(
    "cannot find '{}' in library '{}'");
constexpr ErrorDef<53, const flat::Decl *> ErrCannotReferToMember("cannot refer to member of {}");
constexpr UndocumentedErrorDef<54, const flat::Decl *, std::string_view> ErrMemberNotFound(
    "{} has no member '{}'");
constexpr UndocumentedErrorDef<55, const flat::Element *, VersionRange, Platform,
                               const flat::Element *, const flat::Element *>
    ErrInvalidReferenceToDeprecated(
        "invalid reference to {}, which is deprecated {} of platform '{}' while {} "
        "is not; either remove this reference or mark {} as deprecated");
constexpr UndocumentedErrorDef<56, const flat::Element *, VersionRange, Platform,
                               const flat::Element *, VersionRange, Platform, const flat::Element *>
    ErrInvalidReferenceToDeprecatedOtherPlatform(
        "invalid reference to {}, which is deprecated {} of platform '{}' while {} "
        "is not deprecated {} of platform '{}'; either remove this reference or mark {} as "
        "deprecated");
// ErrIncludeCycle is thrown either as part of SortDeclarations or as part of
// CompileStep, depending on the type of the cycle, because SortDeclarations
// understands the support for boxed recursive structs, while CompileStep
// handles recursive protocols and self-referencing type-aliases.
constexpr ErrorDef<57, std::vector<const flat::Decl *>> ErrIncludeCycle(
    "There is an includes-cycle in declarations: {}");
constexpr ErrorDef<58, flat::Name> ErrAnonymousNameReference("cannot refer to anonymous name {}");
constexpr ErrorDef<59, const flat::Type *> ErrInvalidConstantType("invalid constant type {}");
constexpr ErrorDef<60> ErrCannotResolveConstantValue("unable to resolve constant value");
constexpr ErrorDef<61> ErrOrOperatorOnNonPrimitiveValue(
    "Or operator can only be applied to primitive-kinded values");
constexpr UndocumentedErrorDef<62, flat::Name, std::string_view> ErrNewTypesNotAllowed(
    "newtypes not allowed: type declaration {} defines a new type of the existing {} type, which "
    "is not yet supported");
constexpr ErrorDef<63, flat::Name> ErrExpectedValueButGotType(
    "{} is a type, but a value was expected");
constexpr ErrorDef<64, flat::Name, flat::Name> ErrMismatchedNameTypeAssignment(
    "mismatched named type assignment: cannot define a constant or default value of type {} "
    "using a value of type {}");
constexpr ErrorDef<65, const flat::Constant *, const flat::Type *, const flat::Type *>
    ErrTypeCannotBeConvertedToType("{} (type {}) cannot be converted to type {}");
constexpr ErrorDef<66, const flat::Constant *, const flat::Type *> ErrConstantOverflowsType(
    "{} overflows type {}");
constexpr ErrorDef<67> ErrBitsMemberMustBePowerOfTwo("bits members must be powers of two");
constexpr ErrorDef<68, std::string_view> ErrFlexibleEnumMemberWithMaxValue(
    "flexible enums must not have a member with a value of {}, which is "
    "reserved for the unknown value. either: remove the member, change its "
    "value to something else, or explicitly specify the unknown value with "
    "the @unknown attribute. see "
    "<https://fuchsia.dev/fuchsia-src/reference/fidl/language/attributes#unknown> "
    "for more info.");
constexpr ErrorDef<69, const flat::Type *> ErrBitsTypeMustBeUnsignedIntegralPrimitive(
    "bits may only be of unsigned integral primitive type, found {}");
constexpr ErrorDef<70, const flat::Type *> ErrEnumTypeMustBeIntegralPrimitive(
    "enums may only be of integral primitive type, found {}");
constexpr ErrorDef<71> ErrUnknownAttributeOnStrictEnumMember(
    "the @unknown attribute can be only be used on flexible enum members.");
constexpr ErrorDef<72> ErrUnknownAttributeOnMultipleEnumMembers(
    "the @unknown attribute can be only applied to one enum member.");
constexpr ErrorDef<73> ErrComposingNonProtocol("This declaration is not a protocol");
constexpr ErrorDef<74, flat::Decl::Kind> ErrInvalidMethodPayloadLayoutClass(
    "cannot use {} as a request/response; must use a struct, table, or union");
constexpr ErrorDef<75, const flat::Type *> ErrInvalidMethodPayloadType(
    "invalid request/response type '{}'; must use a struct, table, or union");
constexpr RetiredDef<76, std::string_view> ErrResponsesWithErrorsMustNotBeEmpty(
    "must define success type of method '{}'");
constexpr ErrorDef<77, std::string_view> ErrEmptyPayloadStructs(
    "method '{}' cannot have an empty struct as a payload, prefer omitting the payload altogether");
constexpr ErrorDef<78, std::string_view, SourceSpan> ErrDuplicateMethodName(
    "multiple protocol methods named '{}'; previous was at {}");
constexpr ErrorDef<79, std::string_view, std::string_view, SourceSpan, std::string_view>
    ErrDuplicateMethodNameCanonical(
        "protocol method '{}' conflicts with method '{}' from {}; both are "
        "represented by the canonical form '{}'");
constexpr UndocumentedErrorDef<80> ErrGeneratedZeroValueOrdinal("Ordinal value 0 disallowed.");
constexpr UndocumentedErrorDef<81, SourceSpan, std::string_view> ErrDuplicateMethodOrdinal(
    "Multiple methods with the same ordinal in a protocol; previous was at {}. "
    "Consider using attribute @selector(\"{}\") to change the name used to "
    "calculate the ordinal.");
constexpr UndocumentedErrorDef<82> ErrInvalidSelectorValue(
    "invalid selector value, must be a method name or a fully qualified method name");
constexpr UndocumentedErrorDef<83> ErrFuchsiaIoExplicitOrdinals(
    "fuchsia.io must have explicit ordinals (https://fxbug.dev/77623)");
constexpr ErrorDef<84> ErrPayloadStructHasDefaultMembers(
    "default values are not allowed on members of request/response structs");
constexpr ErrorDef<85, std::string_view, SourceSpan> ErrDuplicateServiceMemberName(
    "multiple service members named '{}'; previous was at {}");
constexpr UndocumentedErrorDef<86> ErrStrictUnionMustHaveNonReservedMember(
    "strict unions must have at least one non-reserved member");
constexpr ErrorDef<87, std::string_view, std::string_view, SourceSpan, std::string_view>
    ErrDuplicateServiceMemberNameCanonical(
        "service member '{}' conflicts with member '{}' from {}; both are "
        "represented by the canonical form '{}'");
constexpr UndocumentedErrorDef<88> ErrOptionalServiceMember("service members cannot be optional");
constexpr ErrorDef<89, std::string_view, SourceSpan> ErrDuplicateStructMemberName(
    "multiple struct fields named '{}'; previous was at {}");
constexpr ErrorDef<90, std::string_view, std::string_view, SourceSpan, std::string_view>
    ErrDuplicateStructMemberNameCanonical(
        "struct field '{}' conflicts with field '{}' from {}; both are represented "
        "by the canonical form '{}'");
constexpr UndocumentedErrorDef<91, std::string_view, const flat::Type *> ErrInvalidStructMemberType(
    "struct field {} has an invalid default type {}");
constexpr ErrorDef<92> ErrTooManyTableOrdinals(
    "table contains too many ordinals; tables are limited to 64 ordinals");
constexpr ErrorDef<93> ErrMaxOrdinalNotTable(
    "the 64th ordinal of a table may only contain a table type");
constexpr ErrorDef<94, SourceSpan> ErrDuplicateTableFieldOrdinal(
    "multiple table fields with the same ordinal; previous was at {}");
constexpr ErrorDef<95, std::string_view, SourceSpan> ErrDuplicateTableFieldName(
    "multiple table fields named '{}'; previous was at {}");
constexpr ErrorDef<96, std::string_view, std::string_view, SourceSpan, std::string_view>
    ErrDuplicateTableFieldNameCanonical(
        "table field '{}' conflicts with field '{}' from {}; both are represented "
        "by the canonical form '{}'");
constexpr ErrorDef<97, SourceSpan> ErrDuplicateUnionMemberOrdinal(
    "multiple union fields with the same ordinal; previous was at {}");
constexpr ErrorDef<98, std::string_view, SourceSpan> ErrDuplicateUnionMemberName(
    "multiple union members named '{}'; previous was at {}");
constexpr ErrorDef<99, std::string_view, std::string_view, SourceSpan, std::string_view>
    ErrDuplicateUnionMemberNameCanonical(
        "union member '{}' conflicts with member '{}' from {}; both are represented "
        "by the canonical form '{}'");
constexpr ErrorDef<100, uint64_t> ErrNonDenseOrdinal(
    "missing ordinal {} (ordinals must be dense); consider marking it reserved");
constexpr ErrorDef<101> ErrCouldNotResolveSizeBound("unable to resolve size bound");
constexpr ErrorDef<102, std::string_view> ErrCouldNotResolveMember("unable to resolve {} member");
constexpr ErrorDef<103, std::string_view> ErrCouldNotResolveMemberDefault(
    "unable to resolve {} default value");
constexpr ErrorDef<104> ErrCouldNotResolveAttributeArg("unable to resolve attribute argument");
constexpr ErrorDef<105, std::string_view, std::string_view, SourceSpan> ErrDuplicateMemberName(
    "multiple {} members named '{}'; previous was at {}");
constexpr ErrorDef<106, std::string_view, std::string_view, std::string_view, SourceSpan,
                   std::string_view>
    ErrDuplicateMemberNameCanonical(
        "{} member '{}' conflicts with member '{}' from {}; both are "
        "represented by the canonical form '{}'");
constexpr ErrorDef<107, std::string_view, std::string_view, std::string_view, SourceSpan>
    ErrDuplicateMemberValue(
        "value of {} member '{}' conflicts with previously declared member '{}' at {}");
constexpr ErrorDef<108, std::string_view, SourceSpan> ErrDuplicateResourcePropertyName(
    "multiple resource properties named '{}'; previous was at {}");
constexpr ErrorDef<109, std::string_view, std::string_view, SourceSpan, std::string_view>
    ErrDuplicateResourcePropertyNameCanonical(
        "resource property '{}' conflicts with property '{}' from {}; both are "
        "represented by the canonical form '{}'");
constexpr ErrorDef<110, flat::Name, std::string_view, std::string_view, flat::Name>
    ErrTypeMustBeResource(
        "'{}' may contain handles (due to member '{}'), so it must "
        "be declared with the `resource` modifier: `resource {} {}`");
constexpr ErrorDef<111, flat::Name, uint32_t, uint32_t> ErrInlineSizeExceedsLimit(
    "'{}' has an inline size of {} bytes, which exceeds the maximum allowed "
    "inline size of {} bytes");
// TODO(fxbug.dev/70399): As part of consolidating name resolution, these should
// be grouped into a single "expected foo but got bar" error, along with
// ErrExpectedValueButGotType.
constexpr ErrorDef<112> ErrOnlyClientEndsInServices("service members must be client_end:P");
constexpr ErrorDef<113, std::string_view, std::string_view, std::string_view, std::string_view>
    ErrMismatchedTransportInServices(
        "service member {} is over the {} transport, but member {} is over the {} transport. "
        "Multiple transports are not allowed.");
constexpr ErrorDef<114, types::Openness, flat::Name, types::Openness, flat::Name>
    ErrComposedProtocolTooOpen(
        "{} protocol '{}' cannot compose {} protocol '{}'; composed protocol may not be more open "
        "than composing protocol");
constexpr ErrorDef<115, types::Openness> ErrFlexibleTwoWayMethodRequiresOpenProtocol(
    "flexible two-way method may only be defined in an open protocol, not {}");
constexpr ErrorDef<116, std::string_view> ErrFlexibleOneWayMethodInClosedProtocol(
    "flexible {} may only be defined in an open or ajar protocol, not closed");
constexpr ErrorDef<117, std::string_view, std::string_view, const flat::Decl *>
    ErrHandleUsedInIncompatibleTransport(
        "handle of type {} may not be sent over transport {} used by {}");
constexpr ErrorDef<118, std::string_view, std::string_view, const flat::Decl *>
    ErrTransportEndUsedInIncompatibleTransport(
        "client_end / server_end of transport type {} may not be sent over transport {} used by "
        "{}");
constexpr ErrorDef<119, std::string_view> ErrEventErrorSyntaxDeprecated(
    "Event '{}' uses the error syntax. This is deprecated (see fxbug.dev/99924)");
constexpr ErrorDef<120, const flat::Attribute *> ErrInvalidAttributePlacement(
    "placement of attribute '{}' disallowed here");
constexpr ErrorDef<121, const flat::Attribute *> ErrDeprecatedAttribute(
    "attribute '{}' is deprecated");
constexpr ErrorDef<122, std::string_view, SourceSpan> ErrDuplicateAttribute(
    "duplicate attribute '{}'; previous was at {}");
constexpr ErrorDef<123, std::string_view, std::string_view, SourceSpan, std::string_view>
    ErrDuplicateAttributeCanonical(
        "attribute '{}' conflicts with attribute '{}' from {}; both are "
        "represented by the canonical form '{}'");
constexpr ErrorDef<124, const flat::AttributeArg *, const flat::Attribute *>
    ErrCanOnlyUseStringOrBool(
        "argument '{}' on user-defined attribute '{}' cannot be a numeric "
        "value; use a bool or string instead");
constexpr ErrorDef<125> ErrAttributeArgMustNotBeNamed(
    "attributes that take a single argument must not name that argument");
constexpr ErrorDef<126, const flat::Constant *> ErrAttributeArgNotNamed(
    "attributes that take multiple arguments must name all of them explicitly, but '{}' was not");
constexpr ErrorDef<127, const flat::Attribute *, std::string_view> ErrMissingRequiredAttributeArg(
    "attribute '{}' is missing the required '{}' argument");
constexpr ErrorDef<128, const flat::Attribute *> ErrMissingRequiredAnonymousAttributeArg(
    "attribute '{}' is missing its required argument");
constexpr ErrorDef<129, const flat::Attribute *, std::string_view> ErrUnknownAttributeArg(
    "attribute '{}' does not support the '{}' argument");
constexpr ErrorDef<130, const flat::Attribute *, std::string_view, SourceSpan>
    ErrDuplicateAttributeArg(
        "attribute '{}' provides the '{}' argument multiple times; previous was at {}");
constexpr ErrorDef<131, const flat::Attribute *, std::string_view, std::string_view, SourceSpan,
                   std::string_view>
    ErrDuplicateAttributeArgCanonical(
        "attribute '{}' argument '{}' conflicts with argument '{}' from {}; both "
        "are represented by the canonical form '{}'");
constexpr ErrorDef<132, const flat::Attribute *> ErrAttributeDisallowsArgs(
    "attribute '{}' does not support arguments");
constexpr ErrorDef<133, std::string_view, const flat::Attribute *> ErrAttributeArgRequiresLiteral(
    "argument '{}' of attribute '{}' does not support referencing constants; "
    "please use a literal instead");
constexpr UndocumentedErrorDef<134, const flat::Attribute *> ErrAttributeConstraintNotSatisfied(
    "declaration did not satisfy constraint of attribute '{}'");
constexpr UndocumentedErrorDef<135, std::string_view> ErrInvalidDiscoverableName(
    "invalid @discoverable name '{}'; must follow the format 'the.library.name.TheProtocolName'");
constexpr UndocumentedErrorDef<136, flat::Name> ErrTableCannotBeSimple(
    "union '{}' is not a simple type, so it cannot be used in "
    "@for_deprecated_c_bindings");
constexpr UndocumentedErrorDef<137, flat::Name> ErrUnionCannotBeSimple(
    "table '{}' is not a simple type, so it cannot be used in "
    "@for_deprecated_c_bindings");
constexpr UndocumentedErrorDef<138, std::string_view> ErrElementMustBeSimple(
    "element '{}' does not have a simple type, so it cannot be used in "
    "@for_deprecated_c_bindings");
constexpr UndocumentedErrorDef<139, uint32_t, uint32_t> ErrTooManyBytes(
    "too large: only {} bytes allowed, but {} bytes found");
constexpr UndocumentedErrorDef<140, uint32_t, uint32_t> ErrTooManyHandles(
    "too many handles: only {} allowed, but {} found");
constexpr UndocumentedErrorDef<141> ErrInvalidErrorType(
    "invalid error type: must be int32, uint32 or an enum thereof");
constexpr UndocumentedErrorDef<142, std::string_view, std::set<std::string_view>>
    ErrInvalidTransportType("invalid transport type: got {} expected one of {}");
constexpr UndocumentedErrorDef<143, const flat::Attribute *, std::string_view> ErrBoundIsTooBig(
    "'{}' bound of '{}' is too big");
constexpr UndocumentedErrorDef<144, const flat::Attribute *, std::string_view>
    ErrUnableToParseBound("unable to parse '{}' bound of '{}'");
constexpr WarningDef<145, std::string_view, std::string_view> WarnAttributeTypo(
    "suspect attribute with name '{}'; did you mean '{}'?");
constexpr UndocumentedErrorDef<146> ErrInvalidGeneratedName(
    "generated name must be a valid identifier");
constexpr UndocumentedErrorDef<147> ErrAvailableMissingArguments(
    "at least one argument is required: 'added', 'deprecated', or 'removed'");
constexpr UndocumentedErrorDef<148> ErrNoteWithoutDeprecation(
    "the argument 'note' cannot be used without 'deprecated'");
constexpr UndocumentedErrorDef<149> ErrPlatformNotOnLibrary(
    "the argument 'platform' can only be used on the library's @available attribute");
constexpr UndocumentedErrorDef<150> ErrLibraryAvailabilityMissingAdded(
    "missing 'added' argument on the library's @available attribute");
constexpr UndocumentedErrorDef<151, std::vector<std::string_view>> ErrMissingLibraryAvailability(
    "to use the @available attribute here, you must also annotate the "
    "`library {};` declaration in one of the library's files");
constexpr UndocumentedErrorDef<152, std::string_view> ErrInvalidPlatform(
    "invalid platform '{}'; must match the regex [a-z][a-z0-9_]*");
constexpr UndocumentedErrorDef<153, uint64_t> ErrInvalidVersion(
    "invalid version '{}'; must be an integer from 1 to 2^63-1 inclusive, or "
    "the special constant `HEAD`");
constexpr UndocumentedErrorDef<154> ErrInvalidAvailabilityOrder(
    "invalid availability; must have added <= deprecated < removed");
constexpr UndocumentedErrorDef<155, const flat::AttributeArg *, std::string_view,
                               const flat::AttributeArg *, std::string_view, SourceSpan,
                               std::string_view, std::string_view, std::string_view>
    ErrAvailabilityConflictsWithParent(
        "the argument {}={} conflicts with {}={} at {}; a child element "
        "cannot be {} {} its parent element is {}");
constexpr UndocumentedErrorDef<156, flat::Name> ErrCannotBeOptional("{} cannot be optional");
constexpr UndocumentedErrorDef<157, flat::Name> ErrMustBeAProtocol("{} must be a protocol");
constexpr ErrorDef<158, flat::Name> ErrCannotBoundTwice("{} cannot bound twice");
constexpr UndocumentedErrorDef<159, flat::Name> ErrStructCannotBeOptional(
    "structs can no longer be marked optional; please use the new syntax, "
    "`box<{}>`");
constexpr UndocumentedErrorDef<160, flat::Name> ErrCannotIndicateOptionalTwice(
    "{} is already optional, cannot indicate optionality twice");
constexpr UndocumentedErrorDef<161, flat::Name> ErrMustHaveNonZeroSize(
    "{} must have non-zero size");
constexpr ErrorDef<162, flat::Name, size_t, size_t> ErrWrongNumberOfLayoutParameters(
    "{} expected {} layout parameter(s), but got {}");
constexpr UndocumentedErrorDef<163> ErrMultipleConstraintDefinitions(
    "cannot specify multiple constraint sets on a type");
constexpr UndocumentedErrorDef<164, flat::Name, size_t, size_t> ErrTooManyConstraints(
    "{} expected at most {} constraints, but got {}");
constexpr UndocumentedErrorDef<165> ErrExpectedType("expected type but got a literal or constant");
constexpr UndocumentedErrorDef<166, flat::Name> ErrUnexpectedConstraint(
    "{} failed to resolve constraint");
constexpr ErrorDef<167, flat::Name> ErrCannotConstrainTwice("{} cannot add additional constraint");
constexpr UndocumentedErrorDef<168, flat::Name> ErrProtocolConstraintRequired(
    "{} requires a protocol as its first constraint");
// The same error as ErrCannotBeOptional, but with a more specific message since the
// optionality of boxes may be confusing
constexpr UndocumentedErrorDef<169> ErrBoxCannotBeOptional(
    "cannot specify optionality for box, boxes are optional by default");
constexpr UndocumentedErrorDef<170> ErrBoxedTypeCannotBeOptional(
    "no double optionality, boxes are already optional");
constexpr UndocumentedErrorDef<171, flat::Name> ErrCannotBeBoxed(
    "type {} cannot be boxed, try using optional instead");
constexpr ErrorDef<172, flat::Name> ErrResourceMustBeUint32Derived("resource {} must be uint32");
constexpr ErrorDef<173, flat::Name> ErrResourceMissingSubtypeProperty(
    "resource {} expected to have the subtype property, but it was missing");
constexpr RetiredDef<174, flat::Name> ErrResourceMissingRightsProperty(
    "resource {} expected to have the rights property, but it was missing");
constexpr ErrorDef<175, flat::Name> ErrResourceSubtypePropertyMustReferToEnum(
    "the subtype property must be an enum, but wasn't in resource {}");
constexpr RetiredDef<176> ErrHandleSubtypeMustReferToResourceSubtype(
    "the subtype must be a constant referring to the resource's subtype enum");
constexpr ErrorDef<177, flat::Name> ErrResourceRightsPropertyMustReferToBits(
    "the rights property must be a uint32 or a uint32-based bits, "
    "but wasn't defined as such in resource {}");
constexpr ErrorDef<178, std::vector<std::string_view>, std::vector<std::string_view>,
                   std::vector<std::string_view>>
    ErrUnusedImport("Library {} imports {} but does not use it. Either use {}, or remove import.");
constexpr UndocumentedErrorDef<179, flat::Name> ErrNewTypeCannotHaveConstraint(
    "{} is a new-type, which cannot carry constraints");
constexpr ErrorDef<180, flat::Name> ErrExperimentalZxCTypesDisallowed(
    "{} is an experimental type that must be enabled by with `--experimental zx_c_types`");
constexpr ErrorDef<181> ErrReferenceInLibraryAttribute(
    "attributes on the 'library' declaration do not support referencing constants");
constexpr ErrorDef<182, const flat::AttributeArg *> ErrLegacyWithoutRemoval(
    "the argument '{}' is not allowed on an element that is never removed");
constexpr ErrorDef<183, const flat::AttributeArg *, std::string_view, const flat::AttributeArg *,
                   std::string_view, SourceSpan>
    ErrLegacyConflictsWithParent(
        "the argument {}={} conflicts with {}={} at {}; a child element "
        "cannot be added back at LEGACY if its parent is removed");
constexpr ErrorDef<184, std::string_view> ErrUnexpectedControlCharacter(
    "unexpected control character in string literal; use the Unicode escape `\\u{{}}` instead");
constexpr ErrorDef<185> ErrUnicodeEscapeMissingBraces(
    "Unicode escape must use braces, like `\\u{a}` for U+000A");
constexpr ErrorDef<186> ErrUnicodeEscapeUnterminated(
    "Unicode escape is missing a closing brace '}'");
constexpr ErrorDef<187> ErrUnicodeEscapeEmpty("Unicode escape must have at least 1 hex digit");
constexpr ErrorDef<188> ErrUnicodeEscapeTooLong("Unicode escape must have at most 6 hex digits");
constexpr ErrorDef<189, std::string_view> ErrUnicodeEscapeTooLarge(
    "invalid Unicode code point '{}'; maximum is 10FFFF");
constexpr ErrorDef<190, flat::Name> ErrSimpleProtocolMustBeClosed(
    "@for_deprecated_c_bindings annotated protocol {} must be closed");

}  // namespace fidl

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_DIAGNOSTICS_H_
