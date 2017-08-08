// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "parser/parser.h" // for FileCtx

constexpr size_t kMaxArgs = 8;

extern const std::map<std::string, std::string> rust_overrides;
extern const std::map<std::string, std::string> rust_primitives;
extern const std::map<std::string, std::string> rust_reserved_words;

struct ArraySpec {
    enum Kind : uint32_t {
        IN,
        OUT,
        INOUT
    };

    Kind kind;
    uint32_t count;
    std::string name;

    std::string kind_str() const;
    bool assign_kind(const std::vector<std::string>& attrs);
    std::string to_string() const;
};

struct TypeSpec {
    std::string name;
    std::string type;
    std::vector<std::string> attributes;
    ArraySpec* arr_spec = nullptr;

    void debug_dump() const;
    std::string to_string() const;
    std::string as_cpp_declaration(bool is_wrapped) const;
    std::string as_rust_declaration() const;
    std::string as_cpp_cast(const std::string& arg) const;
};

struct Syscall {
    FileCtx fc;
    std::string name;
    int index = -1;
    std::vector<TypeSpec> ret_spec;
    std::vector<TypeSpec> arg_spec;
    std::vector<std::string> attributes;

    Syscall(const FileCtx& sc_fc, const std::string& sc_name)
        : fc(sc_fc), name(sc_name) {}

    bool is_vdso() const;
    bool is_noreturn() const;
    bool is_blocking() const;
    bool is_internal() const;
    size_t num_kernel_args() const;
    void for_each_kernel_arg(const std::function<void(const TypeSpec&)>& cb) const;
    void for_each_return(const std::function<void(const TypeSpec&)>& cb) const;
    bool validate() const;
    void assign_index(int* next_index);
    bool valid_array_count(const TypeSpec& ts) const;
    void print_error(const char* what) const;
    void debug_dump() const;
    std::string return_type() const;
    bool is_void_return() const;
    bool will_wrap(const std::string& type) const;
    std::string maybe_wrap(const std::string& type) const;
};

const std::string map_override(
    const std::string& name, const std::map<std::string, std::string>& overrides);
const bool has_attribute(const char* attr, const std::vector<std::string>& attrs);
const void dump_attributes(const std::vector<std::string>& attrs);
