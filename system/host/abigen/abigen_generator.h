// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fstream>
#include <functional>
#include <list>
#include <map>
#include <string>
#include <vector>

#include "generator.h"
#include "types.h"

const std::map<std::string, std::string>& get_type_to_default_suffix();
const std::map<std::string, Generator&>& get_type_to_generator();

class AbigenGenerator {
public:
    AbigenGenerator(bool verbose)
        : verbose_(verbose) {}
    bool AddSyscall(Syscall&& syscall);
    bool Generate(const std::map<std::string, std::string>& type_to_filename);
    bool verbose() const;
    void AppendRequirement(Requirement&& req);
    void SetTopDescription(TopDescription&& td);

private:
    bool generate_one(const std::string& output_file,
                      Generator& generator, const std::string& type);
    void print_error(const char* what, const std::string& file);

    std::list<Syscall> calls_;
    std::vector<Requirement> pending_requirements_;
    TopDescription pending_top_description_;
    int next_index_ = 0;
    const bool verbose_;
};
