// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <string>
#include <vector>

#include "types.h"

#include "syscall_parser.h"

using std::string;
using std::vector;

bool is_identifier_keyword(const string& iden) {
    if (iden == "syscall" ||
        iden == "returns" ||
        iden == "optional" ||
        iden == "IN" || iden == "OUT" || iden == "INOUT") {
        return true;
    }
    return false;
}

bool vet_identifier(const string& iden, const FileCtx& fc) {
    if (iden.empty()) {
        fc.print_error("expecting idenfier", "");
        return false;
    }

    if (is_identifier_keyword(iden)) {
        fc.print_error("identifier cannot be keyword or attribute", iden);
        return false;
    }
    if (!isalpha(iden[0])) {
        fc.print_error("identifier should start with a-z|A-Z", string(iden));
        return false;
    }
    return true;
}

bool parse_param_attributes(TokenStream* ts, vector<string>* attrs) {
    while (ts->peek_next() != ")" && ts->peek_next() != ",") {
        auto attr = ts->next();
        attrs->push_back(attr);
    }
    return true;
}

bool parse_arrayspec(TokenStream* ts, TypeSpec* type_spec) {
    std::string name;
    uint32_t count = 0;

    if (ts->next() != "[")
        return false;

    if (ts->next().empty())
        return false;

    auto c = ts->curr()[0];

    if (isalpha(c)) {
        if (!vet_identifier(ts->curr(), ts->filectx()))
            return false;
        name = ts->curr();

    } else if (isdigit(c)) {
        count = c - '0';
        if (ts->curr().size() > 1 || count == 0 || count > 9) {
            ts->filectx().print_error("only 1-9 explicit array count allowed", "");
            return false;
        }
    } else {
        ts->filectx().print_error("expected array specifier", "");
        return false;
    }

    if (name == type_spec->name) {
        ts->filectx().print_error("invalid name for an array specifier", name);
        return false;
    }

    if (ts->next() != "]") {
        ts->filectx().print_error("expected", "]");
        return false;
    }

    type_spec->arr_spec = new ArraySpec{ArraySpec::IN, count, name};
    return true;
}

bool parse_typespec(TokenStream* ts, TypeSpec* type_spec) {
    if (ts->peek_next() == ":") {
        auto name = ts->curr();
        if (!vet_identifier(name, ts->filectx()))
            return false;

        type_spec->name = name;

        ts->next();
        if (ts->next().empty())
            return false;
    }

    auto type = ts->curr();
    if (!vet_identifier(type, ts->filectx()))
        return false;

    type_spec->type = type;

    if (ts->peek_next() == "[" && !parse_arrayspec(ts, type_spec)) {
        return false;
    }

    if (!parse_param_attributes(ts, &type_spec->attributes)) {
        return false;
    }

    if (type_spec->arr_spec && !type_spec->arr_spec->assign_kind(type_spec->attributes)) {
        ts->filectx().print_error("expected", "IN, INOUT or OUT");
        return false;
    }
    return true;
}

bool parse_argpack(TokenStream* ts, vector<TypeSpec>* v) {
    if (ts->curr() != "(") {
        ts->filectx().print_error("expected", "(");
        return false;
    }

    while (true) {
        if (ts->next() == ")")
            break;

        if (v->size() > 0) {
            if (ts->curr() != ",") {
                ts->filectx().print_error("expected", ", or :");
                return false;
            }
            ts->next();
        }

        TypeSpec type_spec;

        if (!parse_typespec(ts, &type_spec))
            return false;
        v->emplace_back(type_spec);
    }
    return true;
}

bool process_comment(SysgenGenerator* parser, TokenStream& ts) {
    return true;
}

bool process_syscall(SysgenGenerator* parser, TokenStream& ts) {
    auto name = ts.next();

    if (!vet_identifier(name, ts.filectx()))
        return false;

    Syscall syscall{ts.filectx(), name};

    // Every entry gets the special catch-all "*" attribute.
    syscall.attributes.push_back("*");

    while (true) {
        auto maybe_attr = ts.next();
        if (maybe_attr[0] != '(') {
            syscall.attributes.push_back(maybe_attr);
        } else {
            break;
        }
    }

    if (!parse_argpack(&ts, &syscall.arg_spec))
        return false;

    auto return_spec = ts.next();

    if (return_spec == "returns") {
        ts.next();

        if (!parse_argpack(&ts, &syscall.ret_spec)) {
            return false;
        }
        if (syscall.ret_spec.size() > 1) {
            std::for_each(syscall.ret_spec.begin() + 1, syscall.ret_spec.end(),
                          [](TypeSpec& type_spec) {
                              type_spec.arr_spec = new ArraySpec{ArraySpec::OUT, 1, ""};
                          });
        }
    } else if (return_spec != ";") {
        ts.filectx().print_error("expected", ";");
        return false;
    }

    return parser->AddSyscall(syscall);
}
