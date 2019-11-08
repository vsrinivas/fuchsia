// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FILD_CODEC_RULE_PARSER_H_
#define SRC_LIB_FILD_CODEC_RULE_PARSER_H_

namespace fidl_codec {

class DescriptionExpression;
class ExpressionVisitor;
class Type;

class Expression {
 public:
  Expression() = default;
  virtual ~Expression() = default;

  virtual DescriptionExpression* CastToDescriptionExpression() { return nullptr; }

  virtual void Visit(ExpressionVisitor* visitor) const = 0;
};

class HandleExpression : public Expression {
 public:
  HandleExpression() = default;

  void Visit(ExpressionVisitor* visitor) const override;
};

class MessageExpression : public Expression {
 public:
  MessageExpression() = default;

  void Visit(ExpressionVisitor* visitor) const override;
};

class DescriptionExpression : public Expression {
 public:
  DescriptionExpression(std::string_view handle_type, std::unique_ptr<Expression> expression)
      : handle_type_(handle_type), expression_(std::move(expression)) {}

  const std::string& handle_type() const { return handle_type_; }
  const Expression* expression() const { return expression_.get(); }
  const fidl_codec::Type* decoding_type() const { return decoding_type_.get(); }
  bool array_decoding() const { return array_decoding_; }

  void SetDecoding(std::unique_ptr<fidl_codec::Type> decoding_type, bool array_decoding) {
    decoding_type_ = std::move(decoding_type);
    array_decoding_ = array_decoding;
  }

  DescriptionExpression* CastToDescriptionExpression() override { return this; }

  void Visit(ExpressionVisitor* visitor) const override;

 private:
  const std::string handle_type_;
  const std::unique_ptr<Expression> expression_;
  std::unique_ptr<fidl_codec::Type> decoding_type_;
  bool array_decoding_ = false;
};

class AccessExpression : public Expression {
 public:
  AccessExpression(std::unique_ptr<Expression> expression, std::string_view field)
      : expression_(std::move(expression)), field_(field) {}

  const Expression* expression() const { return expression_.get(); }
  const std::string& field() const { return field_; }

  void Visit(ExpressionVisitor* visitor) const override;

 private:
  const std::unique_ptr<Expression> expression_;
  const std::string field_;
};

class DivExpression : public Expression {
 public:
  DivExpression(std::unique_ptr<Expression> left, std::unique_ptr<Expression> right)
      : left_(std::move(left)), right_(std::move(right)) {}

  const Expression* left() const { return left_.get(); }
  const Expression* right() const { return right_.get(); }

  void Visit(ExpressionVisitor* visitor) const override;

 private:
  const std::unique_ptr<Expression> left_;
  const std::unique_ptr<Expression> right_;
};

class Assignment {
 public:
  Assignment(std::unique_ptr<Expression> destination, std::unique_ptr<Expression> source)
      : destination_(std::move(destination)), source_(std::move(source)) {}

  const Expression* destination() const { return destination_.get(); }
  const Expression* source() const { return source_.get(); }

 private:
  const std::unique_ptr<Expression> destination_;
  const std::unique_ptr<Expression> source_;
};

class Rule {
 public:
  Rule() = default;

  Rule* AddRequestAssignment(std::unique_ptr<Assignment> assignment) {
    request_assignments_.emplace_back(std::move(assignment));
    return this;
  }

  const std::vector<std::unique_ptr<Assignment>>& request_assignments() const {
    return request_assignments_;
  }
  const std::vector<std::unique_ptr<Assignment>>& response_assignments() const {
    return response_assignments_;
  }

 private:
  std::vector<std::unique_ptr<Assignment>> request_assignments_;
  std::vector<std::unique_ptr<Assignment>> response_assignments_;
};

class ExpressionVisitor {
 public:
  ExpressionVisitor() = default;
  virtual void VisitHandleExpression(const HandleExpression* expression) = 0;
  virtual void VisitMessageExpression(const MessageExpression* expression) = 0;
  virtual void VisitDescriptionExpression(const DescriptionExpression* expression) = 0;
  virtual void VisitAccessExpression(const AccessExpression* expression) = 0;
  virtual void VisitDivExpression(const DivExpression* expression) = 0;
};

}  // namespace fidl_codec

#endif  // SRC_LIB_FILD_CODEC_RULE_PARSER_H_
