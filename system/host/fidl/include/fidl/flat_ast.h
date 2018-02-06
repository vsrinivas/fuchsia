// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_FLAT_AST_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_FLAT_AST_H_

#include <stdint.h>

#include <memory>
#include <vector>

#include "ast.h"

namespace fidl {
namespace flat {

struct Ordinal {
    Ordinal(std::unique_ptr<ast::NumericLiteral> literal, uint32_t value)
        : literal_(std::move(literal)), value_(value) {}

    uint32_t Value() const { return value_; }

private:
    std::unique_ptr<ast::NumericLiteral> literal_;
    uint32_t value_;
};

// TODO(TO-701) Handle multipart names.
struct Name {
    Name()
        : name_(nullptr) {}

    explicit Name(std::unique_ptr<ast::Identifier> name)
        : name_(std::move(name)) {}

    const ast::Identifier* get() const { return name_.get(); }

    bool operator<(const Name& other) const {
        return name_ < other.name_;
    }

private:
    std::unique_ptr<ast::Identifier> name_;
};

struct Const {
    Const(Name name, std::unique_ptr<ast::Type> type, std::unique_ptr<ast::Constant> value)
        : name(std::move(name)), type(std::move(type)), value(std::move(value)) {}
    Name name;
    std::unique_ptr<ast::Type> type;
    std::unique_ptr<ast::Constant> value;
};

struct Enum {
    struct Member {
        Member(Name name, std::unique_ptr<ast::Constant> value)
            : name(std::move(name)), value(std::move(value)) {}
        Name name;
        std::unique_ptr<ast::Constant> value;
    };

    Enum(Name name, std::unique_ptr<ast::PrimitiveType> type, std::vector<Member> members)
        : name(std::move(name)), type(std::move(type)), members(std::move(members)) {}

    Name name;
    std::unique_ptr<ast::PrimitiveType> type;
    std::vector<Member> members;
};

struct Interface {
    struct Method {
        struct Parameter {
            Parameter(std::unique_ptr<ast::Type> type, std::unique_ptr<ast::Identifier> name)
                : type(std::move(type)), name(std::move(name)) {}
            std::unique_ptr<ast::Type> type;
            std::unique_ptr<ast::Identifier> name;
        };

        Method(Method&&) = default;
        Method& operator=(Method&&) = default;

        Method(Ordinal ordinal, std::unique_ptr<ast::Identifier> name, bool has_request,
               std::vector<Parameter> maybe_request, bool has_response,
               std::vector<Parameter> maybe_response)
            : ordinal(std::move(ordinal)), name(std::move(name)), has_request(has_request),
              maybe_request(std::move(maybe_request)), has_response(has_response),
              maybe_response(std::move(maybe_response)) {
            assert(has_request || has_response);
        }

        Ordinal ordinal;
        std::unique_ptr<ast::Identifier> name;
        bool has_request;
        std::vector<Parameter> maybe_request;
        bool has_response;
        std::vector<Parameter> maybe_response;
    };

    Interface(Name name, std::vector<Method> methods)
        : name(std::move(name)), methods(std::move(methods)) {}

    Name name;
    std::vector<Method> methods;
};

struct Struct {
    struct Member {
        Member(std::unique_ptr<ast::Type> type, std::unique_ptr<ast::Identifier> name,
               std::unique_ptr<ast::Constant> default_value)
            : type(std::move(type)), name(std::move(name)),
              default_value(std::move(default_value)) {}
        std::unique_ptr<ast::Type> type;
        std::unique_ptr<ast::Identifier> name;
        std::unique_ptr<ast::Constant> default_value;
    };

    Struct(Name name, std::vector<Member> members)
        : name(std::move(name)), members(std::move(members)) {}

    Name name;
    std::vector<Member> members;
};

struct Union {
    struct Member {
        Member(std::unique_ptr<ast::Type> type, std::unique_ptr<ast::Identifier> name)
            : type(std::move(type)), name(std::move(name)) {}
        std::unique_ptr<ast::Type> type;
        std::unique_ptr<ast::Identifier> name;
    };

    Union(Name name, std::vector<Member> members)
        : name(std::move(name)), members(std::move(members)) {}

    Name name;
    std::vector<Member> members;
};

} // namespace flat
} // namespace fidl

#endif // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_FLAT_AST_H_
