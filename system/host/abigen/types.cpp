// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>

#include "types.h"

using std::string;
using std::vector;

const std::map<string, string> rust_overrides = {
    {"any[]IN", "*const u8"},
    {"any[]OUT", "*mut u8"},
    {"any[]INOUT", "*mut u8"}};

const std::map<string, string> rust_primitives = {
    {"int8_t", "i8"},
    {"int16_t", "i16"},
    {"int32_t", "i32"},
    {"int64_t", "i64"},
    {"uint8_t", "u8"},
    {"uint16_t", "u16"},
    {"uint32_t", "u32"},
    {"uint64_t", "u64"},
    {"size_t", "usize"},
    {"uintptr_t", "usize"},
    {"int", "isize"},
    {"char", "u8"},
    {"float", "f32"},
    {"double", "f64"},
};

const std::map<string, string> rust_reserved_words = {
    {"proc", "proc_"},
};

bool has_attribute(const char* attr, const vector<string>& attrs) {
    return std::find(attrs.begin(), attrs.end(), attr) != attrs.end();
}

void dump_attributes(const vector<string>& attrs) {
    for (auto& a : attrs) {
        fprintf(stderr, "%s ", a.c_str());
    }
    fprintf(stderr, "\n");
}

string ArraySpec::kind_str() const {
    switch (kind) {
    case IN:
        return "IN";
    case OUT:
        return "OUT";
    default:
        return "INOUT";
    }
}

string ArraySpec::kind_lowercase_str() const {
    switch (kind) {
    case IN:
        return "in";
    case OUT:
        return "out";
    default:
        return "inout";
    }
}

bool ArraySpec::assign_kind(const vector<string>& attrs) {
    if (has_attribute("IN", attrs)) {
        kind = ArraySpec::IN;
    } else if (has_attribute("OUT", attrs)) {
        kind = ArraySpec::OUT;
    } else if (has_attribute("INOUT", attrs)) {
        kind = ArraySpec::INOUT;
    } else {
        return false;
    }
    return true;
}

string ArraySpec::to_string() const {
    return "[]" + kind_str();
}

const string map_override(const string& name, const std::map<string, string>& overrides) {
    auto ft = overrides.find(name);
    return (ft == overrides.end()) ? name : ft->second;
}

string TypeSpec::to_string() const {
    return type + (arr_spec ? arr_spec->to_string() : string());
}

string TypeSpec::as_cpp_declaration(bool is_wrapped) const {
    if (!arr_spec) {
        return type + " " + name;
    }

    if (is_wrapped &&
        arr_spec->kind == ArraySpec::OUT && arr_spec->count == 1 &&
        type == "zx_handle_t") {
        return "user_out_handle* " + name;
    }

    string modifier = arr_spec->kind == ArraySpec::IN ? "const " : "";
    string ptr_type = type == "any" ? "void" : type;

    if (is_wrapped) {
        // This policy strings here matches the enum defined in the
        // kernel's user_ptr.h.
        string policy = arr_spec->kind_lowercase_str();
        return "user_" + policy + "_ptr<" + modifier + ptr_type + "> " + name;
    }
    return modifier + ptr_type + "* " + name;
}

string TypeSpec::as_rust_declaration() const {
    auto overridden = map_override(to_string(), rust_overrides);
    auto scalar_type = map_override(type, rust_primitives);
    auto safe_name = map_override(name, rust_reserved_words);

    if (overridden != to_string()) {
        return safe_name + ": " + overridden;
    } else if (!arr_spec) {
        return safe_name + ": " + scalar_type;
    } else {
        string ret = safe_name + ": ";
        if (arr_spec->kind == ArraySpec::IN) {
            ret += "*const ";
        } else {
            ret += "*mut ";
        }

        ret += scalar_type;
        if (arr_spec->count > 1)
            ret += " " + std::to_string(arr_spec->count);
        return ret;
    }
}

string TypeSpec::as_cpp_cast(const string& arg) const {
    if (!arr_spec) {
        return "static_cast<" + type + ">(" + arg + ")";
    }

    string modifier = arr_spec->kind == ArraySpec::IN ? "const " : "";
    string cast_type = type == "any" ? "void*" : type + "*";
    return "reinterpret_cast<" + modifier + cast_type + ">(" + arg + ")";
}

bool Syscall::is_vdso() const {
    return has_attribute("vdsocall", attributes);
}

bool Syscall::is_noreturn() const {
    return has_attribute("noreturn", attributes);
}

bool Syscall::is_blocking() const {
    return has_attribute("blocking", attributes);
}

bool Syscall::is_internal() const {
    return has_attribute("internal", attributes);
}

size_t Syscall::num_kernel_args() const {
    return is_noreturn() ? arg_spec.size() : arg_spec.size() + ret_spec.size() - 1;
}

void Syscall::for_each_return(const std::function<void(const TypeSpec&)>& cb) const {
    if (ret_spec.size() > 1) {
        std::for_each(ret_spec.begin() + 1, ret_spec.end(), cb);
    }
}

void Syscall::for_each_kernel_arg(const std::function<void(const TypeSpec&)>& cb) const {
    std::for_each(arg_spec.begin(), arg_spec.end(), cb);
    for_each_return(cb);
}

bool Syscall::validate() const {
    if (ret_spec.size() > 0 && is_noreturn()) {
        print_error("noreturn should have zero return arguments");
        return false;
    }

    if (num_kernel_args() > kMaxArgs) {
        print_error("invalid number of arguments");
        return false;
    }

    if (ret_spec.size() >= 1 && !ret_spec[0].name.empty()) {
        print_error("the first return argument cannot be named, yet...");
        return false;
    }

    if (is_blocking() &&
        (ret_spec.size() == 0 || ret_spec[0].type != "zx_status_t")) {
        print_error("blocking must have first return be of type zx_status_t");
        return false;
    }

    if (is_vdso() && is_internal()) {
        print_error("vdsocall cannot be internal");
        return false;
    }

    bool valid_args = true;
    for_each_kernel_arg([this, &valid_args](const TypeSpec& arg) {
        if (arg.name.empty()) {
            print_error("all arguments need to be named, except the first return");
            valid_args = false;
        }
        if (arg.arr_spec) {
            if (!validate_array_spec(arg)) {
                valid_args = false;
            }
        }
    });
    return valid_args;
}

void Syscall::assign_index(int* next_index) {
    if (!is_vdso())
        index = (*next_index)++;
}

bool Syscall::validate_array_spec(const TypeSpec& ts) const {
    if (ts.arr_spec->count > 0)
        return true;
    // find arguments that represent the array count.
    for (const string& multiplier : ts.arr_spec->multipliers) {
        auto arg = std::find_if(arg_spec.begin(), arg_spec.end(), [&] (const TypeSpec& a) {
            return a.name == multiplier;
        });
        if (arg == arg_spec.end()) {
            std::string err = "invalid array spec for " + ts.name
                + ": '" + multiplier + "' does not refer to an argument";
            print_error(err.c_str());
            return false;
        }
        // TODO:cpu also enforce INOUT here.
        if (arg->arr_spec && arg->arr_spec->count != 1u) {
            std::string err = "invalid array spec for " + ts.name
                + ": '" + multiplier + "' refers to an array of size != 1";
            return false;
        }
    }
    return true;
}

void Syscall::print_error(const char* what) const {
    fprintf(stderr, "error: %s  : %s\n", name.c_str(), what);
}

string Syscall::return_type() const {
    if (ret_spec.empty()) {
        return "void";
    }
    return ret_spec[0].to_string();
}

bool Syscall::is_void_return() const {
    return return_type() == "void";
}
