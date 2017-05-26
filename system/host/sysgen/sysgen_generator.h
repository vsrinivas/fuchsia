// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fstream>
#include <functional>
#include <list>
#include <map>
#include <string>

#include "generator.h"
#include "types.h"

const std::map<std::string, std::string>& get_type_to_default_suffix();
const std::map<std::string, Generator&>& get_type_to_generator();

class SysgenGenerator {
public:
    SysgenGenerator(bool verbose)
        : verbose_(verbose) {}
    bool AddSyscall(Syscall& syscall);
    bool Generate(const std::map<std::string, std::string>& type_to_filename);
    bool verbose() const;

private:
    bool generate_one(const std::string& output_file,
                      Generator& generator, const std::string& type);
    void print_error(const char* what, const std::string& file);

    std::list<Syscall> calls_;
    int next_index_ = 0;
    const bool verbose_;
};
