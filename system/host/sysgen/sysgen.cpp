// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <map>
#include <numeric>
#include <string>

#include "syscall_parser.h"
#include "sysgen_generator.h"
#include "types.h"

using std::string;

constexpr Dispatch<SysgenGenerator> sysgen_table[] = {
    // comments start with '#' and terminate at the end of line.
    {"#", nullptr, process_comment},
    // sycalls start with 'syscall' and terminate with ';'.
    {"syscall", ";", process_syscall},
    // table terminator.
    {nullptr, nullptr, nullptr}};

// =================================== driver ====================================================

int main(int argc, char* argv[]) {
    string output_prefix = "generated";
    bool verbose = false;
    bool generate_all = false;
    std::map<string, string> type_to_filename;

    argc--;
    argv++;
    while (argc > 0) {
        const string command(argv[0]);
        if (command[0] != '-')
            break;
        else if (get_type_to_generator().find(command.substr(1)) != get_type_to_generator().end()) {
            string type = command.substr(1);
            type_to_filename[type] = string(argv[1]);
            argc--;
            argv++;
        } else if (command == "-a") {
            generate_all = true;
        } else if (command == "-v") {
            verbose = true;
        } else if (command == "-o") {
            if (argc < 2) {
                fprintf(stderr, "no output prefix given\n");
                return -1;
            }
            output_prefix.assign(argv[1]);
            argc--;
            argv++;
        } else if (command == "-h") {
            fprintf(stderr, "usage: sysgen [-a] [-v] [-o output_prefix] "
                            "[-<type> filename] file1 ... fileN\n");
            const string delimiter = ", ";
            const string valid_types =
                std::accumulate(
                    get_type_to_default_suffix().begin(),
                    get_type_to_default_suffix().end(), std::string(),
                    [delimiter](const std::string& s,
                                const std::pair<const std::string, std::string>& p) {
                        return s + (s.empty() ? std::string() : delimiter) + p.first;
                    });
            fprintf(stderr, "\n       Valid <type>s: %s\n", valid_types.c_str());
            return 0;
        } else {
            fprintf(stderr, "unknown option: %s\n", command.c_str());
            return -1;
        }
        argc--;
        argv++;
    }
    if (argc < 1) {
        fprintf(stderr, "no syscall-spec input given\n");
        return -1;
    }

    // Use defaults for anything not specified.
    if (generate_all) {
        for (auto& entry : get_type_to_default_suffix()) {
            if (type_to_filename.find(entry.first) == type_to_filename.end()) {
                type_to_filename[entry.first] = output_prefix + entry.second;
            }
        }
    }

    SysgenGenerator generator(verbose);

    for (int ix = 0; ix < argc; ix++) {
        if (!run_parser(&generator, sysgen_table, argv[ix], verbose))
            return 1;
    }
    return generator.Generate(type_to_filename) ? 0 : 1;
}
