// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/template_type_extractor.h"

#include "src/developer/debug/zxdb/expr/expr_token.h"
#include "src/lib/fxl/logging.h"

namespace zxdb {

namespace {

// Tracks the level of nesting of brackets.
struct Nesting {
  Nesting(size_t i, ExprTokenType e) : opening_index(i), end(e) {}

  size_t opening_index = 0;                     // Index of opening bracket.
  ExprTokenType end = ExprTokenType::kInvalid;  // Expected closing bracket.
};

// A table of operators that need special handling. These are ones that can interfere with the
// parsing. Things like "operator+" are skipped fine using the normal code path of "word" +
// "punctuation" so don't need to be here for the current limited use case.
//
// This is in order we should evaluate it, so if one is a subset of another (e.g. "operator+" is a
// subset of "operator++"), the more specific one should be first.
struct OperatorRecord {
  ExprTokenType first;
  ExprTokenType second;
};
const OperatorRecord kOperators[] = {
    {ExprTokenType::kLess, ExprTokenType::kLess},        // <<
    {ExprTokenType::kLess, ExprTokenType::kInvalid},     // <
    {ExprTokenType::kGreater, ExprTokenType::kGreater},  // >>
    {ExprTokenType::kGreater, ExprTokenType::kInvalid},  // >
    {ExprTokenType::kComma, ExprTokenType::kInvalid},    // ,
};

bool IsNamelikeToken(const ExprToken& token) {
  return token.type() == ExprTokenType::kName || token.type() == ExprTokenType::kTrue ||
         token.type() == ExprTokenType::kFalse || token.type() == ExprTokenType::kConst ||
         token.type() == ExprTokenType::kVolatile;
}

// Returns true if the token at the given index needs a space before it to separate it from the
// previous token. The first_index is the index of the first token being considered for type
// extraction (so we don't consider the boundary before this).
bool NeedsSpaceBefore(const std::vector<ExprToken>& tokens, size_t first_index, size_t index) {
  FXL_DCHECK(first_index <= index);
  if (first_index == index)
    return false;  // Also catches index == 0.

  // Names always need a space between then. A name here is any word, so "const Foo" would be an
  // example.
  if (IsNamelikeToken(tokens[index - 1]) && IsNamelikeToken(tokens[index]))
    return true;

  // Put a space after a comma. This is undesirable in the case of "operator," appearing as in
  // "template<CmpOp a = operator,>" but not a big deal.
  if (tokens[index - 1].type() == ExprTokenType::kComma)
    return true;

  // Most other things can go next to each other as far as valid C++ goes. These are some cases that
  // this does incorrectly, see the comment above ExtractTemplateType() for why this isn't so bad
  // and how it could be improved.
  return false;
}

// |*index| points to the index of the operator token. It will be updated to
// point to the last token consumed.
void HandleOperator(const std::vector<ExprToken>& tokens, size_t* index, std::string* result) {
  // Always append "operator" itself.
  result->append(tokens[*index].value());
  if (tokens.size() - 1 == *index)
    return;  // "operator" at end of stream, just append it.

  // 0 when not found, otherwise # tokens matched after "operator".
  int matched_tokens = 0;

  // The second token we're looking for.
  ExprTokenType second_type =
      tokens.size() > *index + 2 ? tokens[*index + 2].type() : ExprTokenType::kInvalid;
  for (const auto& cur_op : kOperators) {
    if (cur_op.first == tokens[*index + 1].type()) {
      // First character matched.
      if (cur_op.second == ExprTokenType::kInvalid) {
        // Anything matches, we found it.
        matched_tokens = 1;
        break;
      }

      // The following token should also match, and the two tokens should be
      // adjacent in the input stream.
      if (cur_op.second == second_type &&
          tokens[*index + 1].byte_offset() + 1 == tokens[*index + 2].byte_offset()) {
        matched_tokens = 2;
        break;
      }
    }
  }

  // Append any matched tokens. If no token is matched, it's probably an invalid operator
  // specification (doesn't matter since we're just identifying and canonicalizing).
  if (matched_tokens >= 1) {
    result->append(tokens[*index + 1].value());
    if (matched_tokens == 2)
      result->append(tokens[*index + 2].value());
  }
  *index += matched_tokens;
}

}  // namespace

// This doesn't handle some evil things, mostly around "operator" keywords:
//
//   template<CmpOp a = operator> > void DoBar();
//   template<CmpOp a = operator>>> void DoBar();
//   template<CmpOp a = operator,> void DoBar();
//
//   auto foo = operator + + 1;
//
// Currently it assumes all operators can be put next to each other without affecting meaning. When
// we're canonicalizing types for the purposes of string comparisons, this is almost certainly the
// case. If we start using the output from this function for more things, we'll want to handle these
// cases better.
//
// To address this, I'm thinking we should look for the "operator" keyword. Then look up the
// following tokens in a table of valid C++ operator function names to consume those that are
// actually part of the operator name (this needs some careful handling of spaces
// (ExprToken.byte_offset), since "operator++" and "operator ++" are the same thing but "operator
// ++" and "operator + +" are different).
//
// When we have this lookahead for "operator>" we can remove the "PreviousTokenIsOperatorKeyword"
// code.
TemplateTypeResult ExtractTemplateType(const std::vector<ExprToken>& tokens, size_t begin_token) {
  TemplateTypeResult result;

  bool inhibit_next_space = false;

  std::vector<Nesting> nesting;
  size_t i = begin_token;
  for (; i < tokens.size(); i++) {
    ExprTokenType type = tokens[i].type();
    if (type == ExprTokenType::kLeftSquare) {
      // [
      nesting.emplace_back(i, ExprTokenType::kRightSquare);
    } else if (type == ExprTokenType::kLeftParen) {
      // (
      nesting.emplace_back(i, ExprTokenType::kRightParen);
    } else if (type == ExprTokenType::kLess) {
      // < (the sequences "operator<" and "operator<<" were handled when we got the "operator"
      //    token).
      nesting.emplace_back(i, ExprTokenType::kGreater);
    } else if (nesting.empty() &&
               (type == ExprTokenType::kGreater || type == ExprTokenType::kRightParen ||
                type == ExprTokenType::kComma)) {
      // These tokens mark the end of a type when seen without nesting. Usually this marks the end
      // of the enclosing cast or template.
      break;
    } else if (!nesting.empty() && type == nesting.back().end) {
      // Found the closing token for a previous opening one.
      nesting.pop_back();
    } else if (type == ExprTokenType::kName && tokens[i].value() == "operator") {
      // Possible space before "operator".
      if (NeedsSpaceBefore(tokens, begin_token, i))
        result.canonical_name.push_back(' ');
      HandleOperator(tokens, &i, &result.canonical_name);

      // This prevents adding a space after the "," that would normally go there for a normal comma.
      inhibit_next_space = true;
      continue;  // Skip the code at the bottom that appends the token.
    }

    if (!inhibit_next_space && NeedsSpaceBefore(tokens, begin_token, i))
      result.canonical_name.push_back(' ');
    inhibit_next_space = false;

    result.canonical_name += tokens[i].value();
  }

  if (nesting.empty()) {
    result.success = true;
    result.end_token = i;
  } else {
    // Unterminated thing, tell the caller where it started.
    result.success = false;
    result.unmatched_error_token = nesting.back().opening_index;
    result.canonical_name.clear();
    result.end_token = tokens.size();
  }
  return result;
}

}  // namespace zxdb
