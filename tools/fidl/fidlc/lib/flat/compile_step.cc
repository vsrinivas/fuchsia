// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/flat/compile_step.h"

#include <algorithm>
#include <optional>

#include "fidl/diagnostics.h"
#include "fidl/flat/attribute_schema.h"
#include "fidl/flat/type_resolver.h"
#include "fidl/flat_ast.h"
#include "fidl/names.h"
#include "fidl/ordinals.h"

namespace fidl::flat {

// See RFC-0132 for the origin of this table limit.
constexpr size_t kMaxTableOrdinals = 64;

void CompileStep::RunImpl() {
  CompileAttributeList(library()->attributes.get());

  for (auto& [name, decl] : library()->declarations) {
    CompileDecl(decl);
  }
}

namespace {

class ScopeInsertResult {
 public:
  explicit ScopeInsertResult(std::unique_ptr<SourceSpan> previous_occurrence)
      : previous_occurrence_(std::move(previous_occurrence)) {}

  static ScopeInsertResult Ok() { return ScopeInsertResult(nullptr); }
  static ScopeInsertResult FailureAt(SourceSpan previous) {
    return ScopeInsertResult(std::make_unique<SourceSpan>(previous));
  }

  bool ok() const { return previous_occurrence_ == nullptr; }

  const SourceSpan& previous_occurrence() const {
    assert(!ok());
    return *previous_occurrence_;
  }

 private:
  std::unique_ptr<SourceSpan> previous_occurrence_;
};

template <typename T>
class Scope {
 public:
  ScopeInsertResult Insert(const T& t, SourceSpan span) {
    auto iter = scope_.find(t);
    if (iter != scope_.end()) {
      return ScopeInsertResult::FailureAt(iter->second);
    } else {
      scope_.emplace(t, span);
      return ScopeInsertResult::Ok();
    }
  }

  typename std::map<T, SourceSpan>::const_iterator begin() const { return scope_.begin(); }

  typename std::map<T, SourceSpan>::const_iterator end() const { return scope_.end(); }

 private:
  std::map<T, SourceSpan> scope_;
};

using Ordinal64Scope = Scope<uint64_t>;

std::optional<std::pair<uint64_t, SourceSpan>> FindFirstNonDenseOrdinal(
    const Ordinal64Scope& scope) {
  uint64_t last_ordinal_seen = 0;
  for (const auto& ordinal_and_loc : scope) {
    uint64_t next_expected_ordinal = last_ordinal_seen + 1;
    if (ordinal_and_loc.first != next_expected_ordinal) {
      return std::optional{std::make_pair(next_expected_ordinal, ordinal_and_loc.second)};
    }
    last_ordinal_seen = ordinal_and_loc.first;
  }
  return std::nullopt;
}

struct MethodScope {
  Ordinal64Scope ordinals;
  Scope<std::string> canonical_names;
  Scope<const Protocol*> protocols;
};

// A helper class to derive the resourceness of synthesized decls based on their
// members. If the given std::optional<types::Resourceness> is already set
// (meaning the decl is user-defined, not synthesized), this does nothing.
//
// Types added via AddType must already be compiled. In other words, there must
// not be cycles among the synthesized decls.
class DeriveResourceness {
 public:
  explicit DeriveResourceness(std::optional<types::Resourceness>* target)
      : target_(target), derive_(!target->has_value()), result_(types::Resourceness::kValue) {}

  ~DeriveResourceness() {
    if (derive_) {
      *target_ = result_;
    }
  }

  void AddType(const Type* type) {
    if (derive_ && result_ == types::Resourceness::kValue &&
        type->Resourceness() == types::Resourceness::kResource) {
      result_ = types::Resourceness::kResource;
    }
  }

 private:
  std::optional<types::Resourceness>* const target_;
  const bool derive_;
  types::Resourceness result_;
};

// A helper class to track when a Decl is compiling and compiled.
class Compiling {
 public:
  explicit Compiling(Decl* decl, std::vector<const Decl*>& decl_stack)
      : decl_(decl), decl_stack_(decl_stack) {
    decl_->compiling = true;
    decl_stack_.push_back(decl);
  }

  ~Compiling() {
    decl_->compiling = false;
    decl_->compiled = true;
    decl_stack_.pop_back();
  }

 private:
  Decl* decl_;
  // Stack trace of decl compile calls.
  std::vector<const Decl*>& decl_stack_;
};

}  // namespace

std::optional<std::vector<const Decl*>> CompileStep::GetDeclCycle(const Decl* decl) {
  if (!decl->compiled && decl->compiling) {
    auto decl_pos = std::find(decl_stack_.begin(), decl_stack_.end(), decl);
    // Decl should already be in the stack somewhere because compiling is set to
    // true iff the decl is in the decl stack.
    assert(decl_pos != decl_stack_.end());
    // Copy the part of the cycle we care about so Compiling guards can pop
    // normally when returning.
    std::vector<const Decl*> cycle(decl_pos, decl_stack_.end());
    // Add a second instance of the decl at the end of the list so it shows as
    // both the beginning and end of the cycle.
    cycle.push_back(decl);
    return cycle;
  }
  return std::nullopt;
}

void CompileStep::CompileDecl(Decl* decl) {
  if (decl->compiled) {
    return;
  }
  if (auto cycle = GetDeclCycle(decl); cycle) {
    Fail(ErrIncludeCycle, decl->name.span().value(), cycle.value());
    return;
  }
  Compiling guard(decl, decl_stack_);
  switch (decl->kind) {
    case Decl::Kind::kBits:
      CompileBits(static_cast<Bits*>(decl));
      break;
    case Decl::Kind::kConst:
      CompileConst(static_cast<Const*>(decl));
      break;
    case Decl::Kind::kEnum:
      CompileEnum(static_cast<Enum*>(decl));
      break;
    case Decl::Kind::kProtocol:
      CompileProtocol(static_cast<Protocol*>(decl));
      break;
    case Decl::Kind::kResource:
      CompileResource(static_cast<Resource*>(decl));
      break;
    case Decl::Kind::kService:
      CompileService(static_cast<Service*>(decl));
      break;
    case Decl::Kind::kStruct:
      CompileStruct(static_cast<Struct*>(decl));
      break;
    case Decl::Kind::kTable:
      CompileTable(static_cast<Table*>(decl));
      break;
    case Decl::Kind::kUnion:
      CompileUnion(static_cast<Union*>(decl));
      break;
    case Decl::Kind::kTypeAlias:
      CompileTypeAlias(static_cast<TypeAlias*>(decl));
      break;
  }  // switch
}

bool CompileStep::ResolveOrOperatorConstant(Constant* constant, std::optional<const Type*> opt_type,
                                            const ConstantValue& left_operand,
                                            const ConstantValue& right_operand) {
  assert(left_operand.kind == right_operand.kind &&
         "left and right operands of or operator must be of the same kind");
  assert(opt_type && "compiler bug: type inference not implemented for or operator");
  const auto type = UnderlyingType(opt_type.value());
  if (type == nullptr)
    return false;
  if (type->kind != Type::Kind::kPrimitive) {
    return Fail(ErrOrOperatorOnNonPrimitiveValue, constant->span);
  }
  std::unique_ptr<ConstantValue> left_operand_u64;
  std::unique_ptr<ConstantValue> right_operand_u64;
  if (!left_operand.Convert(ConstantValue::Kind::kUint64, &left_operand_u64))
    return false;
  if (!right_operand.Convert(ConstantValue::Kind::kUint64, &right_operand_u64))
    return false;
  NumericConstantValue<uint64_t> result =
      *static_cast<NumericConstantValue<uint64_t>*>(left_operand_u64.get()) |
      *static_cast<NumericConstantValue<uint64_t>*>(right_operand_u64.get());
  std::unique_ptr<ConstantValue> converted_result;
  if (!result.Convert(ConstantValuePrimitiveKind(static_cast<const PrimitiveType*>(type)->subtype),
                      &converted_result))
    return false;
  constant->ResolveTo(std::move(converted_result), type);
  return true;
}

bool CompileStep::ResolveConstant(Constant* constant, std::optional<const Type*> opt_type) {
  assert(constant != nullptr);

  // Prevent re-entry.
  if (constant->compiled)
    return constant->IsResolved();
  constant->compiled = true;

  switch (constant->kind) {
    case Constant::Kind::kIdentifier:
      return ResolveIdentifierConstant(static_cast<IdentifierConstant*>(constant), opt_type);
    case Constant::Kind::kLiteral:
      return ResolveLiteralConstant(static_cast<LiteralConstant*>(constant), opt_type);
    case Constant::Kind::kBinaryOperator: {
      auto binary_operator_constant = static_cast<BinaryOperatorConstant*>(constant);
      if (!ResolveConstant(binary_operator_constant->left_operand.get(), opt_type)) {
        return false;
      }
      if (!ResolveConstant(binary_operator_constant->right_operand.get(), opt_type)) {
        return false;
      }
      switch (binary_operator_constant->op) {
        case BinaryOperatorConstant::Operator::kOr:
          return ResolveOrOperatorConstant(constant, opt_type,
                                           binary_operator_constant->left_operand->Value(),
                                           binary_operator_constant->right_operand->Value());
        default:
          assert(false && "Compiler bug: unhandled binary operator");
      }
      break;
    }
  }
  __builtin_unreachable();
}

ConstantValue::Kind CompileStep::ConstantValuePrimitiveKind(
    const types::PrimitiveSubtype primitive_subtype) {
  switch (primitive_subtype) {
    case types::PrimitiveSubtype::kBool:
      return ConstantValue::Kind::kBool;
    case types::PrimitiveSubtype::kInt8:
      return ConstantValue::Kind::kInt8;
    case types::PrimitiveSubtype::kInt16:
      return ConstantValue::Kind::kInt16;
    case types::PrimitiveSubtype::kInt32:
      return ConstantValue::Kind::kInt32;
    case types::PrimitiveSubtype::kInt64:
      return ConstantValue::Kind::kInt64;
    case types::PrimitiveSubtype::kUint8:
      return ConstantValue::Kind::kUint8;
    case types::PrimitiveSubtype::kUint16:
      return ConstantValue::Kind::kUint16;
    case types::PrimitiveSubtype::kUint32:
      return ConstantValue::Kind::kUint32;
    case types::PrimitiveSubtype::kUint64:
      return ConstantValue::Kind::kUint64;
    case types::PrimitiveSubtype::kFloat32:
      return ConstantValue::Kind::kFloat32;
    case types::PrimitiveSubtype::kFloat64:
      return ConstantValue::Kind::kFloat64;
  }
  assert(false && "Compiler bug: unhandled primitive subtype");
}

bool CompileStep::ResolveIdentifierConstant(IdentifierConstant* identifier_constant,
                                            std::optional<const Type*> opt_type) {
  if (opt_type) {
    assert(TypeCanBeConst(opt_type.value()) &&
           "Compiler bug: resolving identifier constant to non-const-able type!");
  }

  auto decl = library()->LookupDeclByName(identifier_constant->name.memberless_key());
  if (!decl)
    return false;
  CompileDecl(decl);

  const Type* const_type = nullptr;
  const ConstantValue* const_val = nullptr;
  switch (decl->kind) {
    case Decl::Kind::kConst: {
      auto const_decl = static_cast<Const*>(decl);
      if (!const_decl->value->IsResolved()) {
        return false;
      }
      const_type = const_decl->type_ctor->type;
      const_val = &const_decl->value->Value();
      break;
    }
    case Decl::Kind::kEnum: {
      // If there is no member name, fallthrough to default.
      if (auto member_name = identifier_constant->name.member_name(); member_name) {
        auto enum_decl = static_cast<Enum*>(decl);
        const_type = enum_decl->subtype_ctor->type;
        for (auto& member : enum_decl->members) {
          if (member.name.data() == member_name) {
            if (!member.value->IsResolved()) {
              return false;
            }
            const_val = &member.value->Value();
          }
        }
        if (!const_val) {
          return Fail(ErrUnknownEnumMember, identifier_constant->name.span().value(), *member_name);
        }
        break;
      }
      [[fallthrough]];
    }
    case Decl::Kind::kBits: {
      // If there is no member name, fallthrough to default.
      if (auto member_name = identifier_constant->name.member_name(); member_name) {
        auto bits_decl = static_cast<Bits*>(decl);
        const_type = bits_decl->subtype_ctor->type;
        for (auto& member : bits_decl->members) {
          if (member.name.data() == member_name) {
            if (!member.value->IsResolved()) {
              return false;
            }
            const_val = &member.value->Value();
          }
        }
        if (!const_val) {
          return Fail(ErrUnknownBitsMember, identifier_constant->name.span().value(), *member_name);
        }
        break;
      }
      [[fallthrough]];
    }
    default: {
      return Fail(ErrExpectedValueButGotType, identifier_constant->name.span().value(),
                  identifier_constant->name);
    }
  }

  assert(const_val && "Compiler bug: did not set const_val");
  assert(const_type && "Compiler bug: did not set const_type");

  std::unique_ptr<ConstantValue> resolved_val;
  const auto type = opt_type ? opt_type.value() : const_type;
  switch (type->kind) {
    case Type::Kind::kString: {
      if (!TypeIsConvertibleTo(const_type, type))
        goto fail_cannot_convert;

      if (!const_val->Convert(ConstantValue::Kind::kString, &resolved_val))
        goto fail_cannot_convert;
      break;
    }
    case Type::Kind::kPrimitive: {
      auto primitive_type = static_cast<const PrimitiveType*>(type);
      if (!const_val->Convert(ConstantValuePrimitiveKind(primitive_type->subtype), &resolved_val))
        goto fail_cannot_convert;
      break;
    }
    case Type::Kind::kIdentifier: {
      auto identifier_type = static_cast<const IdentifierType*>(type);
      const PrimitiveType* primitive_type;
      switch (identifier_type->type_decl->kind) {
        case Decl::Kind::kEnum: {
          auto enum_decl = static_cast<const Enum*>(identifier_type->type_decl);
          if (!enum_decl->subtype_ctor->type) {
            return false;
          }
          assert(enum_decl->subtype_ctor->type->kind == Type::Kind::kPrimitive);
          primitive_type = static_cast<const PrimitiveType*>(enum_decl->subtype_ctor->type);
          break;
        }
        case Decl::Kind::kBits: {
          auto bits_decl = static_cast<const Bits*>(identifier_type->type_decl);
          assert(bits_decl->subtype_ctor->type->kind == Type::Kind::kPrimitive);
          if (!bits_decl->subtype_ctor->type) {
            return false;
          }
          primitive_type = static_cast<const PrimitiveType*>(bits_decl->subtype_ctor->type);
          break;
        }
        default: {
          assert(false && "Compiler bug: identifier not of const-able type.");
        }
      }

      auto fail_with_mismatched_type = [this, identifier_type,
                                        identifier_constant](const Name& type_name) {
        return Fail(ErrMismatchedNameTypeAssignment, identifier_constant->span,
                    identifier_type->type_decl->name, type_name);
      };

      switch (decl->kind) {
        case Decl::Kind::kConst: {
          if (const_type->name != identifier_type->type_decl->name)
            return fail_with_mismatched_type(const_type->name);
          break;
        }
        case Decl::Kind::kBits:
        case Decl::Kind::kEnum: {
          if (decl->name != identifier_type->type_decl->name)
            return fail_with_mismatched_type(decl->name);
          break;
        }
        default: {
          assert(false && "Compiler bug: identifier not of const-able type.");
        }
      }

      if (!const_val->Convert(ConstantValuePrimitiveKind(primitive_type->subtype), &resolved_val))
        goto fail_cannot_convert;
      break;
    }
    default: {
      assert(false && "Compiler bug: identifier not of const-able type.");
    }
  }

  identifier_constant->ResolveTo(std::move(resolved_val), type);
  return true;

fail_cannot_convert:
  return Fail(ErrTypeCannotBeConvertedToType, identifier_constant->name.span().value(),
              identifier_constant, const_type, type);
}

bool CompileStep::ResolveLiteralConstant(LiteralConstant* literal_constant,
                                         std::optional<const Type*> opt_type) {
  auto inferred_type = InferType(static_cast<flat::Constant*>(literal_constant));
  const Type* type = opt_type ? opt_type.value() : inferred_type;
  if (!TypeIsConvertibleTo(inferred_type, type)) {
    return Fail(ErrTypeCannotBeConvertedToType, literal_constant->literal->span(), literal_constant,
                inferred_type, type);
  }
  switch (literal_constant->literal->kind) {
    case raw::Literal::Kind::kDocComment: {
      auto doc_comment_literal =
          static_cast<raw::DocCommentLiteral*>(literal_constant->literal.get());
      literal_constant->ResolveTo(
          std::make_unique<DocCommentConstantValue>(doc_comment_literal->span().data()),
          &Typespace::kUnboundedStringType);
      return true;
    }
    case raw::Literal::Kind::kString: {
      literal_constant->ResolveTo(
          std::make_unique<StringConstantValue>(literal_constant->literal->span().data()),
          &Typespace::kUnboundedStringType);
      return true;
    }
    case raw::Literal::Kind::kBool: {
      auto bool_literal = static_cast<raw::BoolLiteral*>(literal_constant->literal.get());
      literal_constant->ResolveTo(std::make_unique<BoolConstantValue>(bool_literal->value),
                                  &Typespace::kBoolType);
      return true;
    }
    case raw::Literal::Kind::kNumeric: {
      // Even though `untyped numeric` is convertible to any numeric type, we
      // still need to check for overflows which is done in
      // ResolveLiteralConstantKindNumericLiteral.
      switch (static_cast<const PrimitiveType*>(type)->subtype) {
        case types::PrimitiveSubtype::kInt8:
          return ResolveLiteralConstantKindNumericLiteral<int8_t>(literal_constant, type);
        case types::PrimitiveSubtype::kInt16:
          return ResolveLiteralConstantKindNumericLiteral<int16_t>(literal_constant, type);
        case types::PrimitiveSubtype::kInt32:
          return ResolveLiteralConstantKindNumericLiteral<int32_t>(literal_constant, type);
        case types::PrimitiveSubtype::kInt64:
          return ResolveLiteralConstantKindNumericLiteral<int64_t>(literal_constant, type);
        case types::PrimitiveSubtype::kUint8:
          return ResolveLiteralConstantKindNumericLiteral<uint8_t>(literal_constant, type);
        case types::PrimitiveSubtype::kUint16:
          return ResolveLiteralConstantKindNumericLiteral<uint16_t>(literal_constant, type);
        case types::PrimitiveSubtype::kUint32:
          return ResolveLiteralConstantKindNumericLiteral<uint32_t>(literal_constant, type);
        case types::PrimitiveSubtype::kUint64:
          return ResolveLiteralConstantKindNumericLiteral<uint64_t>(literal_constant, type);
        case types::PrimitiveSubtype::kFloat32:
          return ResolveLiteralConstantKindNumericLiteral<float>(literal_constant, type);
        case types::PrimitiveSubtype::kFloat64:
          return ResolveLiteralConstantKindNumericLiteral<double>(literal_constant, type);
        default:
          assert(false && "compiler bug: should not have any other primitive type reachable");
          return false;
      }
    }
  }  // switch
}

template <typename NumericType>
bool CompileStep::ResolveLiteralConstantKindNumericLiteral(LiteralConstant* literal_constant,
                                                           const Type* type) {
  NumericType value;
  const auto span = literal_constant->literal->span();
  std::string string_data(span.data().data(), span.data().data() + span.data().size());
  switch (utils::ParseNumeric(string_data, &value)) {
    case utils::ParseNumericResult::kSuccess:
      literal_constant->ResolveTo(std::make_unique<NumericConstantValue<NumericType>>(value), type);
      return true;
    case utils::ParseNumericResult::kMalformed:
      // The caller (ResolveLiteralConstant) ensures that the constant kind is
      // a numeric literal, which means that it follows the grammar for
      // numerical types. As a result, an error to parse the data here is due
      // to the data being too large, rather than bad input.
      [[fallthrough]];
    case utils::ParseNumericResult::kOutOfBounds:
      return Fail(ErrConstantOverflowsType, span, literal_constant, type);
  }
}

const Type* CompileStep::InferType(Constant* constant) {
  switch (constant->kind) {
    case Constant::Kind::kLiteral: {
      auto literal = static_cast<const raw::Literal*>(
          static_cast<const LiteralConstant*>(constant)->literal.get());
      switch (literal->kind) {
        case raw::Literal::Kind::kString: {
          auto string_literal = static_cast<const raw::StringLiteral*>(literal);
          auto inferred_size = utils::string_literal_length(string_literal->span().data());
          auto inferred_type = std::make_unique<StringType>(Typespace::kUnboundedStringType.name,
                                                            typespace()->InternSize(inferred_size),
                                                            types::Nullability::kNonnullable);
          return typespace()->Intern(std::move(inferred_type));
        }
        case raw::Literal::Kind::kNumeric:
          return &Typespace::kUntypedNumericType;
        case raw::Literal::Kind::kBool:
          return &Typespace::kBoolType;
        case raw::Literal::Kind::kDocComment:
          return &Typespace::kUnboundedStringType;
      }
      return nullptr;
    }
    case Constant::Kind::kIdentifier:
      if (!ResolveConstant(constant, std::nullopt)) {
        return nullptr;
      }
      return constant->type;
    case Constant::Kind::kBinaryOperator:
      assert(false && "compiler bug: type inference not implemented for binops");
      __builtin_unreachable();
  }
}

bool CompileStep::ResolveAsOptional(Constant* constant) {
  assert(constant);

  if (constant->kind != Constant::Kind::kIdentifier)
    return false;

  // This refers to the `optional` constraint only if it is "optional" AND
  // it is not shadowed by a previous definition.
  // Note that as we improve scoping rules, we would need to allow `fidl.optional`
  // to be the FQN for the `optional` constant.
  auto identifier_constant = static_cast<IdentifierConstant*>(constant);
  auto decl = library()->LookupDeclByName(identifier_constant->name.memberless_key());
  if (decl)
    return false;

  return identifier_constant->name.decl_name() == "optional";
}

void CompileStep::CompileAttributeList(AttributeList* attributes) {
  Scope<std::string> scope;
  for (auto& attribute : attributes->attributes) {
    const auto original_name = attribute->name.data();
    const auto canonical_name = utils::canonicalize(original_name);
    const auto result = scope.Insert(canonical_name, attribute->name);
    if (!result.ok()) {
      const auto previous_span = result.previous_occurrence();
      if (original_name == previous_span.data()) {
        Fail(ErrDuplicateAttribute, attribute->name, original_name, previous_span);
      } else {
        Fail(ErrDuplicateAttributeCanonical, attribute->name, original_name, previous_span.data(),
             previous_span, canonical_name);
      }
    }
    CompileAttribute(attribute.get());
  }
}

void CompileStep::CompileAttribute(Attribute* attribute, bool early) {
  if (attribute->compiled) {
    return;
  }

  Scope<std::string> scope;
  for (auto& arg : attribute->args) {
    if (!arg->name.has_value()) {
      continue;
    }
    const auto original_name = arg->name.value().data();
    const auto canonical_name = utils::canonicalize(original_name);
    const auto result = scope.Insert(canonical_name, arg->name.value());
    if (!result.ok()) {
      const auto previous_span = result.previous_occurrence();
      if (original_name == previous_span.data()) {
        Fail(ErrDuplicateAttributeArg, attribute->span, attribute, original_name, previous_span);
      } else {
        Fail(ErrDuplicateAttributeArgCanonical, attribute->span, attribute, original_name,
             previous_span.data(), previous_span, canonical_name);
      }
    }
  }

  const AttributeSchema& schema = all_libraries()->RetrieveAttributeSchema(attribute,
                                                                           /*warn_on_typo=*/true);
  if (early) {
    assert(schema.CanCompileEarly() && "attribute is not allowed to be compiled early");
  }
  schema.ResolveArgs(this, attribute);
  attribute->compiled = true;
}

// static
void CompileStep::CompileAttributeEarly(Compiler* compiler, Attribute* attribute) {
  CompileStep(compiler).CompileAttribute(attribute, /* early = */ true);
}

const Type* CompileStep::UnderlyingType(const Type* type) {
  if (type->kind != Type::Kind::kIdentifier) {
    return type;
  }
  auto identifier_type = static_cast<const IdentifierType*>(type);
  Decl* decl = library()->LookupDeclByName(identifier_type->name);
  // This is only reachable via ResolveOrOperatorConstant, which is always
  // called from ResolveConstant only after resolving the left and right sides.
  // That process should force declaration of the constant, which should fail
  // before this is reached if the type can't be resolved.
  assert(decl);
  CompileDecl(decl);
  switch (decl->kind) {
    case Decl::Kind::kBits:
      return static_cast<const Bits*>(decl)->subtype_ctor->type;
    case Decl::Kind::kEnum:
      return static_cast<const Enum*>(decl)->subtype_ctor->type;
    default:
      return type;
  }
}

bool CompileStep::TypeCanBeConst(const Type* type) {
  switch (type->kind) {
    case flat::Type::Kind::kString:
      return type->nullability != types::Nullability::kNullable;
    case flat::Type::Kind::kPrimitive:
      return true;
    case flat::Type::Kind::kIdentifier: {
      auto identifier_type = static_cast<const IdentifierType*>(type);
      switch (identifier_type->type_decl->kind) {
        case Decl::Kind::kEnum:
        case Decl::Kind::kBits:
          return true;
        default:
          return false;
      }
    }
    default:
      return false;
  }  // switch
}

bool CompileStep::TypeIsConvertibleTo(const Type* from_type, const Type* to_type) {
  switch (to_type->kind) {
    case flat::Type::Kind::kString: {
      if (from_type->kind != flat::Type::Kind::kString)
        return false;

      auto from_string_type = static_cast<const flat::StringType*>(from_type);
      auto to_string_type = static_cast<const flat::StringType*>(to_type);

      if (to_string_type->nullability == types::Nullability::kNonnullable &&
          from_string_type->nullability != types::Nullability::kNonnullable)
        return false;

      if (to_string_type->max_size->value < from_string_type->max_size->value)
        return false;

      return true;
    }
    case flat::Type::Kind::kPrimitive: {
      auto to_primitive_type = static_cast<const flat::PrimitiveType*>(to_type);
      switch (from_type->kind) {
        case flat::Type::Kind::kUntypedNumeric:
          return to_primitive_type->subtype != types::PrimitiveSubtype::kBool;
        case flat::Type::Kind::kPrimitive:
          break;  // handled below
        default:
          return false;
      }
      auto from_primitive_type = static_cast<const flat::PrimitiveType*>(from_type);
      switch (to_primitive_type->subtype) {
        case types::PrimitiveSubtype::kBool:
          return from_primitive_type->subtype == types::PrimitiveSubtype::kBool;
        default:
          // TODO(pascallouis): be more precise about convertibility, e.g. it
          // should not be allowed to convert a float to an int.
          return from_primitive_type->subtype != types::PrimitiveSubtype::kBool;
      }
    }
    default:
      return false;
  }  // switch
}

void CompileStep::CompileBits(Bits* bits_declaration) {
  CompileAttributeList(bits_declaration->attributes.get());
  for (auto& member : bits_declaration->members) {
    CompileAttributeList(member.attributes.get());
  }

  CompileTypeConstructor(bits_declaration->subtype_ctor.get());
  if (!bits_declaration->subtype_ctor->type) {
    return;
  }

  if (bits_declaration->subtype_ctor->type->kind != Type::Kind::kPrimitive) {
    Fail(ErrBitsTypeMustBeUnsignedIntegralPrimitive, bits_declaration->name.span().value(),
         bits_declaration->subtype_ctor->type);
    return;
  }

  // Validate constants.
  auto primitive_type = static_cast<const PrimitiveType*>(bits_declaration->subtype_ctor->type);
  switch (primitive_type->subtype) {
    case types::PrimitiveSubtype::kUint8: {
      uint8_t mask;
      if (!ValidateBitsMembersAndCalcMask<uint8_t>(bits_declaration, &mask))
        return;
      bits_declaration->mask = mask;
      break;
    }
    case types::PrimitiveSubtype::kUint16: {
      uint16_t mask;
      if (!ValidateBitsMembersAndCalcMask<uint16_t>(bits_declaration, &mask))
        return;
      bits_declaration->mask = mask;
      break;
    }
    case types::PrimitiveSubtype::kUint32: {
      uint32_t mask;
      if (!ValidateBitsMembersAndCalcMask<uint32_t>(bits_declaration, &mask))
        return;
      bits_declaration->mask = mask;
      break;
    }
    case types::PrimitiveSubtype::kUint64: {
      uint64_t mask;
      if (!ValidateBitsMembersAndCalcMask<uint64_t>(bits_declaration, &mask))
        return;
      bits_declaration->mask = mask;
      break;
    }
    case types::PrimitiveSubtype::kBool:
    case types::PrimitiveSubtype::kInt8:
    case types::PrimitiveSubtype::kInt16:
    case types::PrimitiveSubtype::kInt32:
    case types::PrimitiveSubtype::kInt64:
    case types::PrimitiveSubtype::kFloat32:
    case types::PrimitiveSubtype::kFloat64:
      Fail(ErrBitsTypeMustBeUnsignedIntegralPrimitive, bits_declaration->name.span().value(),
           bits_declaration->subtype_ctor->type);
      return;
  }
}

void CompileStep::CompileConst(Const* const_declaration) {
  CompileAttributeList(const_declaration->attributes.get());
  CompileTypeConstructor(const_declaration->type_ctor.get());
  const auto* const_type = const_declaration->type_ctor->type;
  if (!const_type) {
    return;
  }
  if (!TypeCanBeConst(const_type)) {
    Fail(ErrInvalidConstantType, const_declaration->name.span().value(), const_type);
  } else if (!ResolveConstant(const_declaration->value.get(), const_type)) {
    Fail(ErrCannotResolveConstantValue, const_declaration->name.span().value());
  }
}

void CompileStep::CompileEnum(Enum* enum_declaration) {
  CompileAttributeList(enum_declaration->attributes.get());
  for (auto& member : enum_declaration->members) {
    CompileAttributeList(member.attributes.get());
  }

  CompileTypeConstructor(enum_declaration->subtype_ctor.get());
  if (!enum_declaration->subtype_ctor->type) {
    return;
  }

  if (enum_declaration->subtype_ctor->type->kind != Type::Kind::kPrimitive) {
    Fail(ErrEnumTypeMustBeIntegralPrimitive, enum_declaration->name.span().value(),
         enum_declaration->subtype_ctor->type);
    return;
  }

  // Validate constants.
  auto primitive_type = static_cast<const PrimitiveType*>(enum_declaration->subtype_ctor->type);
  enum_declaration->type = primitive_type;
  switch (primitive_type->subtype) {
    case types::PrimitiveSubtype::kInt8: {
      int8_t unknown_value;
      if (ValidateEnumMembersAndCalcUnknownValue<int8_t>(enum_declaration, &unknown_value)) {
        enum_declaration->unknown_value_signed = unknown_value;
      }
      break;
    }
    case types::PrimitiveSubtype::kInt16: {
      int16_t unknown_value;
      if (ValidateEnumMembersAndCalcUnknownValue<int16_t>(enum_declaration, &unknown_value)) {
        enum_declaration->unknown_value_signed = unknown_value;
      }
      break;
    }
    case types::PrimitiveSubtype::kInt32: {
      int32_t unknown_value;
      if (ValidateEnumMembersAndCalcUnknownValue<int32_t>(enum_declaration, &unknown_value)) {
        enum_declaration->unknown_value_signed = unknown_value;
      }
      break;
    }
    case types::PrimitiveSubtype::kInt64: {
      int64_t unknown_value;
      if (ValidateEnumMembersAndCalcUnknownValue<int64_t>(enum_declaration, &unknown_value)) {
        enum_declaration->unknown_value_signed = unknown_value;
      }
      break;
    }
    case types::PrimitiveSubtype::kUint8: {
      uint8_t unknown_value;
      if (ValidateEnumMembersAndCalcUnknownValue<uint8_t>(enum_declaration, &unknown_value)) {
        enum_declaration->unknown_value_unsigned = unknown_value;
      }
      break;
    }
    case types::PrimitiveSubtype::kUint16: {
      uint16_t unknown_value;
      if (ValidateEnumMembersAndCalcUnknownValue<uint16_t>(enum_declaration, &unknown_value)) {
        enum_declaration->unknown_value_unsigned = unknown_value;
      }
      break;
    }
    case types::PrimitiveSubtype::kUint32: {
      uint32_t unknown_value;
      if (ValidateEnumMembersAndCalcUnknownValue<uint32_t>(enum_declaration, &unknown_value)) {
        enum_declaration->unknown_value_unsigned = unknown_value;
      }
      break;
    }
    case types::PrimitiveSubtype::kUint64: {
      uint64_t unknown_value;
      if (ValidateEnumMembersAndCalcUnknownValue<uint64_t>(enum_declaration, &unknown_value)) {
        enum_declaration->unknown_value_unsigned = unknown_value;
      }
      break;
    }
    case types::PrimitiveSubtype::kBool:
    case types::PrimitiveSubtype::kFloat32:
    case types::PrimitiveSubtype::kFloat64:
      Fail(ErrEnumTypeMustBeIntegralPrimitive, enum_declaration->name.span().value(),
           enum_declaration->subtype_ctor->type);
      break;
  }
}

void CompileStep::CompileResource(Resource* resource_declaration) {
  Scope<std::string> scope;

  CompileAttributeList(resource_declaration->attributes.get());
  CompileTypeConstructor(resource_declaration->subtype_ctor.get());
  if (!resource_declaration->subtype_ctor->type) {
    return;
  }

  if (resource_declaration->subtype_ctor->type->kind != Type::Kind::kPrimitive ||
      static_cast<const PrimitiveType*>(resource_declaration->subtype_ctor->type)->subtype !=
          types::PrimitiveSubtype::kUint32) {
    Fail(ErrResourceMustBeUint32Derived, resource_declaration->name.span().value(),
         resource_declaration->name);
  }

  for (auto& property : resource_declaration->properties) {
    CompileAttributeList(property.attributes.get());
    const auto original_name = property.name.data();
    const auto canonical_name = utils::canonicalize(original_name);
    const auto name_result = scope.Insert(canonical_name, property.name);
    if (!name_result.ok()) {
      const auto previous_span = name_result.previous_occurrence();
      if (original_name == previous_span.data()) {
        Fail(ErrDuplicateResourcePropertyName, property.name, original_name, previous_span);
      } else {
        Fail(ErrDuplicateResourcePropertyNameCanonical, property.name, original_name,
             previous_span.data(), previous_span, canonical_name);
      }
    }
    CompileTypeConstructor(property.type_ctor.get());
  }
}

void CompileStep::CompileProtocol(Protocol* protocol_declaration) {
  CompileAttributeList(protocol_declaration->attributes.get());

  MethodScope method_scope;
  auto CheckScopes = [this, &protocol_declaration, &method_scope](const Protocol* protocol,
                                                                  auto Visitor) -> void {
    for (const auto& composed_protocol : protocol->composed_protocols) {
      auto name = composed_protocol.name;
      auto decl = library()->LookupDeclByName(name);
      // TODO(fxbug.dev/7926): Special handling here should not be required, we
      // should first rely on creating the types representing composed
      // protocols.
      if (!decl || decl->kind != Decl::Kind::kProtocol) {
        // No need to report an error here since it was already done by the loop
        // after the definition of CheckScopes (before calling CheckScopes).
        continue;
      }
      auto composed_protocol_declaration = static_cast<const Protocol*>(decl);
      auto span = composed_protocol_declaration->name.span();
      assert(span);
      if (method_scope.protocols.Insert(composed_protocol_declaration, span.value()).ok()) {
        Visitor(composed_protocol_declaration, Visitor);
      } else {
        // Otherwise we have already seen this protocol in
        // the inheritance graph.
      }
    }
    for (const auto& method : protocol->methods) {
      const auto original_name = method.name.data();
      const auto canonical_name = utils::canonicalize(original_name);
      const auto name_result = method_scope.canonical_names.Insert(canonical_name, method.name);
      if (!name_result.ok()) {
        const auto previous_span = name_result.previous_occurrence();
        if (original_name == previous_span.data()) {
          Fail(ErrDuplicateMethodName, method.name, original_name, previous_span);
        } else {
          Fail(ErrDuplicateMethodNameCanonical, method.name, original_name, previous_span.data(),
               previous_span, canonical_name);
        }
      }
      if (!method.generated_ordinal64) {
        // If a composed method failed to compile, we do not associate have a
        // generated ordinal, and proceeding leads to a segfault. Instead,
        // continue to the next method, without reporting additional errors (the
        // error emitted when compiling the composed method is sufficient).
        continue;
      }
      if (method.generated_ordinal64->value == 0) {
        Fail(ErrGeneratedZeroValueOrdinal, method.generated_ordinal64->span());
      }
      auto ordinal_result =
          method_scope.ordinals.Insert(method.generated_ordinal64->value, method.name);
      if (!ordinal_result.ok()) {
        std::string replacement_method(
            fidl::ordinals::GetSelector(method.attributes.get(), method.name));
        replacement_method.push_back('_');
        Fail(ErrDuplicateMethodOrdinal, method.generated_ordinal64->span(),
             ordinal_result.previous_occurrence(), replacement_method);
      }

      // Add a pointer to this method to the protocol_declarations list.
      bool is_composed = protocol_declaration != protocol;
      protocol_declaration->all_methods.emplace_back(&method, is_composed);
    }
  };

  // Before scope checking can occur, ordinals must be generated for each of the
  // protocol's methods, including those that were composed from transitive
  // child protocols.  This means that child protocols must be compiled prior to
  // this one, or they will not have generated_ordinal64s on their methods, and
  // will fail the scope check.  Also check for duplicate composed protocols.
  Scope<const Protocol*> scope;
  for (const auto& composed_protocol : protocol_declaration->composed_protocols) {
    CompileAttributeList(composed_protocol.attributes.get());
    auto span = composed_protocol.name.span().value();
    auto decl = library()->LookupDeclByName(composed_protocol.name);
    if (!decl) {
      Fail(ErrUnknownType, span, composed_protocol.name);
      continue;
    }
    if (decl->kind != Decl::Kind::kProtocol) {
      Fail(ErrComposingNonProtocol, span);
      continue;
    }
    auto result = scope.Insert(static_cast<const Protocol*>(decl), span);
    if (!result.ok()) {
      Fail(ErrProtocolComposedMultipleTimes, span, result.previous_occurrence());
    }
    CompileDecl(decl);
  }
  for (auto& method : protocol_declaration->methods) {
    CompileAttributeList(method.attributes.get());
    auto selector = fidl::ordinals::GetSelector(method.attributes.get(), method.name);
    if (!utils::IsValidIdentifierComponent(selector) &&
        !utils::IsValidFullyQualifiedMethodIdentifier(selector)) {
      Fail(ErrInvalidSelectorValue,
           method.attributes->Get("selector")->GetArg(AttributeArg::kDefaultAnonymousName)->span);
      continue;
    }
    // TODO(fxbug.dev/77623): Remove.
    auto library_name = library()->name;
    if (library_name.size() == 2 && library_name[0] == "fuchsia" && library_name[1] == "io" &&
        selector.find("/") == selector.npos) {
      Fail(ErrFuchsiaIoExplicitOrdinals, method.name);
      continue;
    }
    method.generated_ordinal64 = std::make_unique<raw::Ordinal64>(method_hasher()(
        library_name, protocol_declaration->name.decl_name(), selector, *method.identifier));
  }

  CheckScopes(protocol_declaration, CheckScopes);

  // Ensure that the method's type constructors for request/response payloads actually exist, and
  // are the right kind of layout.
  std::function<void(const SourceSpan&, const Decl*, bool)> CheckPayloadDeclKind =
      [&](const SourceSpan& method_name, const Decl* decl, bool empty_payload_allowed) -> void {
    switch (decl->kind) {
      case Decl::Kind::kStruct: {
        const auto struct_decl = static_cast<const Struct*>(decl);
        if (!empty_payload_allowed && struct_decl->members.empty()) {
          Fail(ErrEmptyPayloadStructs, method_name, method_name.data());
        }
        break;
      }
      case Decl::Kind::kTable:
      case Decl::Kind::kUnion: {
        Fail(ErrNotYetSupportedParameterListType, method_name, decl);
        break;
      }
      case Decl::Kind::kTypeAlias: {
        const auto as_type_alias_decl = static_cast<const TypeAlias*>(decl);
        const Type* aliased_type = as_type_alias_decl->partial_type_ctor->type;
        switch (aliased_type->kind) {
          case Type::Kind::kIdentifier: {
            const auto as_identifier_type = static_cast<const IdentifierType*>(aliased_type);
            CheckPayloadDeclKind(method_name, as_identifier_type->type_decl, empty_payload_allowed);
            break;
          }
          default: {
            Fail(ErrInvalidParameterListType, method_name, decl);
            break;
          }
        }
        break;
      }
      default: {
        Fail(ErrInvalidParameterListType, method_name, decl);
        break;
      }
    }
  };

  // Ensure that structs used as message payloads do not have default values.
  auto CheckNoDefaultMembers = [this](const Decl* decl) -> void {
    switch (decl->kind) {
      case Decl::Kind::kStruct: {
        auto struct_decl = static_cast<const Struct*>(decl);
        for (auto& member : struct_decl->members) {
          if (member.maybe_default_value != nullptr) {
            Fail(ErrPayloadStructHasDefaultMembers, member.name);
            break;
          }
        }

        break;
      }
      default: {
        return;
      }
    }
  };

  for (auto& method : protocol_declaration->methods) {
    if (method.maybe_request != nullptr) {
      const Name name = method.maybe_request->name;
      CompileTypeConstructor(method.maybe_request.get());
      Decl* decl = library()->LookupDeclByName(name);
      if (!method.maybe_request->type || !decl) {
        Fail(ErrUnknownType, name.span().value(), name);
        continue;
      }
      CompileDecl(decl);
      CheckNoDefaultMembers(decl);
      CheckPayloadDeclKind(method.name, decl, false);
    }
    if (method.maybe_response != nullptr) {
      const Name name = method.maybe_response->name;
      CompileTypeConstructor(method.maybe_response.get());
      Decl* decl = library()->LookupDeclByName(name);
      if (!method.maybe_response->type || !decl) {
        Fail(ErrUnknownType, name.span().value(), name);
        continue;
      }
      CompileDecl(decl);

      if (method.has_error) {
        assert(method.maybe_response->type->kind == Type::Kind::kIdentifier);
        auto response_id = static_cast<const flat::IdentifierType*>(method.maybe_response->type);

        assert(response_id->type_decl->kind == Decl::Kind::kStruct);
        auto response_struct = static_cast<const flat::Struct*>(response_id->type_decl);
        const auto* result_union_type =
            static_cast<const flat::IdentifierType*>(response_struct->members[0].type_ctor->type);

        assert(result_union_type->type_decl->kind == Decl::Kind::kUnion);
        const auto* result_union = static_cast<const flat::Union*>(result_union_type->type_decl);
        const auto* success_variant_type = static_cast<const flat::IdentifierType*>(
            result_union->members[0].maybe_used->type_ctor->type);

        if (success_variant_type != nullptr) {
          CheckNoDefaultMembers(success_variant_type->type_decl);
          CheckPayloadDeclKind(method.name, success_variant_type->type_decl, true);
        }
      } else {
        CheckNoDefaultMembers(decl);
        CheckPayloadDeclKind(method.name, decl, false);
      }
    }
  }
}

void CompileStep::CompileService(Service* service_decl) {
  Scope<std::string> scope;

  CompileAttributeList(service_decl->attributes.get());
  for (auto& member : service_decl->members) {
    CompileAttributeList(member.attributes.get());
    const auto original_name = member.name.data();
    const auto canonical_name = utils::canonicalize(original_name);
    const auto name_result = scope.Insert(canonical_name, member.name);
    if (!name_result.ok()) {
      const auto previous_span = name_result.previous_occurrence();
      if (original_name == previous_span.data()) {
        Fail(ErrDuplicateServiceMemberName, member.name, original_name, previous_span);
      } else {
        Fail(ErrDuplicateServiceMemberNameCanonical, member.name, original_name,
             previous_span.data(), previous_span, canonical_name);
      }
    }
    CompileTypeConstructor(member.type_ctor.get());
    if (!member.type_ctor->type) {
      continue;
    }
    if (member.type_ctor->type->kind != Type::Kind::kTransportSide) {
      Fail(ErrOnlyClientEndsInServices, member.name);
      continue;
    }
    const auto transport_side_type = static_cast<const TransportSideType*>(member.type_ctor->type);
    if (transport_side_type->end != TransportSide::kClient) {
      Fail(ErrOnlyClientEndsInServices, member.name);
    }
    if (member.type_ctor->type->nullability != types::Nullability::kNonnullable) {
      Fail(ErrNullableServiceMember, member.name);
    }
  }
}

void CompileStep::CompileStruct(Struct* struct_declaration) {
  Scope<std::string> scope;
  DeriveResourceness derive_resourceness(&struct_declaration->resourceness);

  CompileAttributeList(struct_declaration->attributes.get());
  for (auto& member : struct_declaration->members) {
    CompileAttributeList(member.attributes.get());
    const auto original_name = member.name.data();
    const auto canonical_name = utils::canonicalize(original_name);
    const auto name_result = scope.Insert(canonical_name, member.name);
    if (!name_result.ok()) {
      const auto previous_span = name_result.previous_occurrence();
      if (original_name == previous_span.data()) {
        Fail(ErrDuplicateStructMemberName, member.name, original_name, previous_span);
      } else {
        Fail(ErrDuplicateStructMemberNameCanonical, member.name, original_name,
             previous_span.data(), previous_span, canonical_name);
      }
    }

    CompileTypeConstructor(member.type_ctor.get());
    if (!member.type_ctor->type) {
      continue;
    }
    if (member.maybe_default_value) {
      const auto* default_value_type = member.type_ctor->type;
      if (!TypeCanBeConst(default_value_type)) {
        Fail(ErrInvalidStructMemberType, struct_declaration->name.span().value(),
             NameIdentifier(member.name), default_value_type);
      } else if (!ResolveConstant(member.maybe_default_value.get(), default_value_type)) {
        Fail(ErrCouldNotResolveMemberDefault, member.name, NameIdentifier(member.name));
      }
    }
    derive_resourceness.AddType(member.type_ctor->type);
  }
}

void CompileStep::CompileTable(Table* table_declaration) {
  Scope<std::string> name_scope;
  Ordinal64Scope ordinal_scope;

  CompileAttributeList(table_declaration->attributes.get());
  if (table_declaration->members.size() > kMaxTableOrdinals) {
    Fail(ErrTooManyTableOrdinals, table_declaration->name.span().value());
  }

  for (size_t i = 0; i < table_declaration->members.size(); i++) {
    auto& member = table_declaration->members[i];
    CompileAttributeList(member.attributes.get());
    const auto ordinal_result = ordinal_scope.Insert(member.ordinal->value, member.ordinal->span());
    if (!ordinal_result.ok()) {
      Fail(ErrDuplicateTableFieldOrdinal, member.ordinal->span(),
           ordinal_result.previous_occurrence());
    }
    if (!member.maybe_used) {
      continue;
    }
    auto& member_used = *member.maybe_used;
    const auto original_name = member_used.name.data();
    const auto canonical_name = utils::canonicalize(original_name);
    const auto name_result = name_scope.Insert(canonical_name, member_used.name);
    if (!name_result.ok()) {
      const auto previous_span = name_result.previous_occurrence();
      if (original_name == previous_span.data()) {
        Fail(ErrDuplicateTableFieldName, member_used.name, original_name, previous_span);
      } else {
        Fail(ErrDuplicateTableFieldNameCanonical, member_used.name, original_name,
             previous_span.data(), previous_span, canonical_name);
      }
    }
    CompileTypeConstructor(member_used.type_ctor.get());
    if (!member_used.type_ctor->type) {
      continue;
    }
    if (member_used.type_ctor->type->nullability != types::Nullability::kNonnullable) {
      Fail(ErrNullableTableMember, member_used.name);
    }
    if (i == kMaxTableOrdinals - 1) {
      if (member_used.type_ctor->type->kind != Type::Kind::kIdentifier) {
        Fail(ErrMaxOrdinalNotTable, member_used.name);
      } else {
        auto identifier_type = static_cast<const IdentifierType*>(member_used.type_ctor->type);
        if (identifier_type->type_decl->kind != Decl::Kind::kTable) {
          Fail(ErrMaxOrdinalNotTable, member_used.name);
        }
      }
    }
  }

  if (auto ordinal_and_loc = FindFirstNonDenseOrdinal(ordinal_scope)) {
    auto [ordinal, span] = *ordinal_and_loc;
    Fail(ErrNonDenseOrdinal, span, ordinal);
  }
}

void CompileStep::CompileUnion(Union* union_declaration) {
  Scope<std::string> scope;
  Ordinal64Scope ordinal_scope;
  DeriveResourceness derive_resourceness(&union_declaration->resourceness);

  CompileAttributeList(union_declaration->attributes.get());
  for (const auto& member : union_declaration->members) {
    CompileAttributeList(member.attributes.get());
    const auto ordinal_result = ordinal_scope.Insert(member.ordinal->value, member.ordinal->span());
    if (!ordinal_result.ok()) {
      Fail(ErrDuplicateUnionMemberOrdinal, member.ordinal->span(),
           ordinal_result.previous_occurrence());
    }
    if (!member.maybe_used) {
      continue;
    }
    const auto& member_used = *member.maybe_used;
    const auto original_name = member_used.name.data();
    const auto canonical_name = utils::canonicalize(original_name);
    const auto name_result = scope.Insert(canonical_name, member_used.name);
    if (!name_result.ok()) {
      const auto previous_span = name_result.previous_occurrence();
      if (original_name == previous_span.data()) {
        Fail(ErrDuplicateUnionMemberName, member_used.name, original_name, previous_span);
      } else {
        Fail(ErrDuplicateUnionMemberNameCanonical, member_used.name, original_name,
             previous_span.data(), previous_span, canonical_name);
      }
    }

    CompileTypeConstructor(member_used.type_ctor.get());
    if (!member_used.type_ctor->type) {
      continue;
    }
    if (member_used.type_ctor->type->nullability != types::Nullability::kNonnullable) {
      Fail(ErrNullableUnionMember, member_used.name);
    }
    derive_resourceness.AddType(member_used.type_ctor->type);
  }

  if (auto ordinal_and_loc = FindFirstNonDenseOrdinal(ordinal_scope)) {
    auto [ordinal, span] = *ordinal_and_loc;
    Fail(ErrNonDenseOrdinal, span, ordinal);
  }
}

void CompileStep::CompileTypeAlias(TypeAlias* type_alias) {
  CompileAttributeList(type_alias->attributes.get());

  if (type_alias->partial_type_ctor->name == type_alias->name) {
    // fidlc's current semantics for cases like `alias foo = foo;` is to
    // include the LHS in the scope while compiling the RHS. Note that because
    // of an interaction with a fidlc scoping bug that prevents shadowing builtins,
    // this means that `alias Recursive = Recursive;` will fail with an includes
    // cycle error, but e.g. `alias uint32 = uint32;` won't because the user
    // defined `uint32` fails to shadow the builtin which means that we successfully
    // resolve the RHS. To avoid inconsistent semantics, we need to manually
    // catch this case and fail.
    std::vector<const Decl*> cycle = {static_cast<const Decl*>(type_alias),
                                      static_cast<const Decl*>(type_alias)};
    Fail(ErrIncludeCycle, type_alias->name.span().value(), cycle);
    return;
  }
  CompileTypeConstructor(type_alias->partial_type_ctor.get());
}

void CompileStep::CompileTypeConstructor(TypeConstructor* type_ctor) {
  if (type_ctor->type != nullptr) {
    return;
  }
  TypeResolver resolver(this);
  if (!typespace()->Create(&resolver, type_ctor->name, type_ctor->parameters,
                           type_ctor->constraints, &type_ctor->type, &type_ctor->resolved_params,
                           type_ctor->span)) {
    return;
  }

  // postcondition: compilation sets the Type of the TypeConstructor
  assert(type_ctor->type && "type constructors' type not resolved after compilation");
  VerifyTypeCategory(type_ctor->type, type_ctor->name.span(), AllowedCategories::kTypeOnly);
}

bool CompileStep::VerifyTypeCategory(const Type* type, std::optional<SourceSpan> span,
                                     AllowedCategories category) {
  assert(type && "CompileTypeConstructor did not set Type");
  if (type->kind != Type::Kind::kIdentifier) {
    // we assume that all non-identifier types (i.e. builtins) are actually
    // types (and not e.g. protocols or services).
    return category == AllowedCategories::kProtocolOnly ? Fail(ErrCannotUseType, span.value())
                                                        : true;
  }

  auto identifier_type = static_cast<const IdentifierType*>(type);
  switch (identifier_type->type_decl->kind) {
    // services are never allowed in any context
    case Decl::Kind::kService:
      return Fail(ErrCannotUseService, span.value());
      break;
    case Decl::Kind::kProtocol:
      if (category == AllowedCategories::kTypeOnly)
        return Fail(ErrCannotUseProtocol, span.value());
      break;
    default:
      if (category == AllowedCategories::kProtocolOnly)
        return Fail(ErrCannotUseType, span.value());
      break;
  }
  return true;
}

bool CompileStep::ResolveHandleRightsConstant(Resource* resource, Constant* constant,
                                              const HandleRights** out_rights) {
  auto rights_property = resource->LookupProperty("rights");
  if (!rights_property) {
    return false;
  }

  Decl* rights_decl = library()->LookupDeclByName(rights_property->type_ctor->name);
  if (!rights_decl || rights_decl->kind != Decl::Kind::kBits) {
    return false;
  }

  CompileTypeConstructor(rights_property->type_ctor.get());
  const Type* rights_type = rights_property->type_ctor->type;
  if (!rights_type) {
    return false;
  }

  if (!ResolveConstant(constant, rights_type))
    return false;

  if (out_rights)
    *out_rights = static_cast<const HandleRights*>(&constant->Value());
  return true;
}

bool CompileStep::ResolveHandleSubtypeIdentifier(Resource* resource, Constant* constant,
                                                 uint32_t* out_obj_type) {
  // We only support an extremely limited form of resource suitable for
  // handles here, where it must be:
  // - derived from uint32
  // - have a single properties element
  // - the single property element must be a reference to an enum
  // - the single property must be named "subtype".
  if (constant->kind != Constant::Kind::kIdentifier) {
    return false;
  }
  auto identifier_constant = static_cast<IdentifierConstant*>(constant);
  const Name& handle_subtype_identifier = identifier_constant->name;

  auto subtype_property = resource->LookupProperty("subtype");
  if (!subtype_property) {
    return false;
  }

  Decl* subtype_decl = library()->LookupDeclByName(subtype_property->type_ctor->name);
  if (!subtype_decl || subtype_decl->kind != Decl::Kind::kEnum) {
    return false;
  }

  CompileTypeConstructor(subtype_property->type_ctor.get());
  const Type* subtype_type = subtype_property->type_ctor->type;
  if (!subtype_type) {
    return false;
  }

  auto* subtype_enum = static_cast<Enum*>(subtype_decl);
  for (const auto& member : subtype_enum->members) {
    if (member.name.data() == handle_subtype_identifier.span()->data()) {
      if (!ResolveConstant(member.value.get(), subtype_type)) {
        return false;
      }
      const flat::ConstantValue& value = member.value->Value();
      auto obj_type = static_cast<uint32_t>(
          reinterpret_cast<const flat::NumericConstantValue<uint32_t>&>(value));
      *out_obj_type = obj_type;
      return true;
    }
  }

  return false;
}

bool CompileStep::ResolveSizeBound(Constant* size_constant, const Size** out_size) {
  if (!ResolveConstant(size_constant, &Typespace::kUint32Type)) {
    if (size_constant->kind == Constant::Kind::kIdentifier) {
      auto name = static_cast<IdentifierConstant*>(size_constant)->name;
      if (name.library() == library() && name.decl_name() == "MAX" && !name.member_name()) {
        size_constant->ResolveTo(std::make_unique<Size>(Size::Max()), &Typespace::kUint32Type);
      }
    }
  }
  if (!size_constant->IsResolved()) {
    return false;
  }
  if (out_size) {
    *out_size = static_cast<const Size*>(&size_constant->Value());
  }
  return true;
}

template <typename DeclType, typename MemberType>
bool CompileStep::ValidateMembers(DeclType* decl, MemberValidator<MemberType> validator) {
  assert(decl != nullptr);
  auto checkpoint = reporter()->Checkpoint();

  constexpr const char* decl_type = std::is_same_v<DeclType, Enum> ? "enum" : "bits";

  Scope<std::string> name_scope;
  Scope<MemberType> value_scope;
  for (const auto& member : decl->members) {
    assert(member.value != nullptr && "Compiler bug: member value is null!");

    // Check that the member identifier hasn't been used yet
    const auto original_name = member.name.data();
    const auto canonical_name = utils::canonicalize(original_name);
    const auto name_result = name_scope.Insert(canonical_name, member.name);
    if (!name_result.ok()) {
      const auto previous_span = name_result.previous_occurrence();
      // We can log the error and then continue validating for other issues in the decl
      if (original_name == name_result.previous_occurrence().data()) {
        Fail(ErrDuplicateMemberName, member.name, decl_type, original_name, previous_span);
      } else {
        Fail(ErrDuplicateMemberNameCanonical, member.name, decl_type, original_name,
             previous_span.data(), previous_span, canonical_name);
      }
    }

    if (!ResolveConstant(member.value.get(), decl->subtype_ctor->type)) {
      Fail(ErrCouldNotResolveMember, member.name, decl_type);
      continue;
    }

    MemberType value =
        static_cast<const NumericConstantValue<MemberType>&>(member.value->Value()).value;
    const auto value_result = value_scope.Insert(value, member.name);
    if (!value_result.ok()) {
      const auto previous_span = value_result.previous_occurrence();
      // We can log the error and then continue validating other members for other bugs
      Fail(ErrDuplicateMemberValue, member.name, decl_type, original_name, previous_span.data(),
           previous_span);
    }

    auto err = validator(value, member.attributes.get(), member.name);
    if (err) {
      Report(std::move(err));
    }
  }

  return checkpoint.NoNewErrors();
}

template <typename T>
static bool IsPowerOfTwo(T t) {
  if (t == 0) {
    return false;
  }
  if ((t & (t - 1)) != 0) {
    return false;
  }
  return true;
}

template <typename MemberType>
bool CompileStep::ValidateBitsMembersAndCalcMask(Bits* bits_decl, MemberType* out_mask) {
  static_assert(std::is_unsigned<MemberType>::value && !std::is_same<MemberType, bool>::value,
                "Bits members must be an unsigned integral type!");
  // Each bits member must be a power of two.
  MemberType mask = 0u;
  auto validator = [&mask](MemberType member, const AttributeList*,
                           SourceSpan span) -> std::unique_ptr<Diagnostic> {
    if (!IsPowerOfTwo(member)) {
      return Diagnostic::MakeError(ErrBitsMemberMustBePowerOfTwo, span);
    }
    mask |= member;
    return nullptr;
  };
  if (!ValidateMembers<Bits, MemberType>(bits_decl, validator)) {
    return false;
  }
  *out_mask = mask;
  return true;
}

template <typename MemberType>
bool CompileStep::ValidateEnumMembersAndCalcUnknownValue(Enum* enum_decl,
                                                         MemberType* out_unknown_value) {
  static_assert(std::is_integral<MemberType>::value && !std::is_same<MemberType, bool>::value,
                "Enum members must be an integral type!");

  const auto default_unknown_value = std::numeric_limits<MemberType>::max();
  std::optional<MemberType> explicit_unknown_value;
  for (const auto& member : enum_decl->members) {
    if (!ResolveConstant(member.value.get(), enum_decl->subtype_ctor->type)) {
      // ValidateMembers will resolve each member and report errors.
      continue;
    }
    if (member.attributes->Get("unknown") != nullptr) {
      if (explicit_unknown_value.has_value()) {
        return Fail(ErrUnknownAttributeOnMultipleEnumMembers, member.name);
      }
      explicit_unknown_value =
          static_cast<const NumericConstantValue<MemberType>&>(member.value->Value()).value;
    }
  }

  auto validator = [enum_decl, &explicit_unknown_value](
                       MemberType member, const AttributeList* attributes,
                       SourceSpan span) -> std::unique_ptr<Diagnostic> {
    switch (enum_decl->strictness) {
      case types::Strictness::kStrict:
        if (attributes->Get("unknown") != nullptr) {
          return Diagnostic::MakeError(ErrUnknownAttributeOnStrictEnumMember, span);
        }
        return nullptr;
      case types::Strictness::kFlexible:
        if (member == default_unknown_value && !explicit_unknown_value.has_value()) {
          return Diagnostic::MakeError(ErrFlexibleEnumMemberWithMaxValue, span,
                                       std::to_string(default_unknown_value));
        }
        return nullptr;
    }
  };
  if (!ValidateMembers<Enum, MemberType>(enum_decl, validator)) {
    return false;
  }
  *out_unknown_value = explicit_unknown_value.value_or(default_unknown_value);
  return true;
}

}  // namespace fidl::flat
