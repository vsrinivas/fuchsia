// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>
#include <string>
#include <vector>
#include <map>

#include "parser.h" // for FileCtx

using std::string;
using std::vector;

constexpr size_t kMaxArgs = 8;

extern const std::map<string, string> rust_overrides;
extern const std::map<string, string> rust_primitives;
extern const std::map<string, string> rust_reserved_words;

struct ArraySpec {
    enum Kind : uint32_t {
        IN,
        OUT,
        INOUT
    };

    Kind kind;
    uint32_t count;
    string name;

    string kind_str() const;
    bool assign_kind(const vector<string>& attrs);
    string to_string() const;
};

struct TypeSpec {
    string name;
    string type;
    vector<string> attributes;
    ArraySpec* arr_spec = nullptr;

    void debug_dump() const;
    string to_string() const;
    string as_cpp_declaration(bool is_wrapped) const;
    string as_rust_declaration() const;
    string as_cpp_cast(const string& arg) const;
};


struct Syscall {
    FileCtx fc;
    string name;
    int index = -1;
    std::vector<TypeSpec> ret_spec;
    std::vector<TypeSpec> arg_spec;
    std::vector<string> attributes;

    Syscall(const FileCtx& sc_fc, const string& sc_name)
        : fc(sc_fc), name(sc_name) {}

    bool is_vdso() const;
    bool is_noreturn() const;
    bool is_no_wrap() const;
    bool is_blocking() const;
    size_t num_kernel_args() const;
    void for_each_kernel_arg(const std::function<void(const TypeSpec&)>& cb) const;
    bool validate() const;
    void assign_index(int* next_index);
    bool valid_array_count(const TypeSpec& ts) const;
    void print_error(const char* what) const;
    void debug_dump() const;
    string return_type() const;
    bool is_void_return() const;
    bool will_wrap(const string& type) const;
    string maybe_wrap(const string& type) const;
};

const string map_override(const string& name, const std::map<string, string>& overrides);

const bool has_attribute(const char* attr, const vector<string>& attrs);
const void dump_attributes(const vector<string>& attrs);
