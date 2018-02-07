// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fstream>
#include <string>
#include <vector>

#include <zircon/mdi.h>

class Node {
private:
    mdi_id_t id;
    std::string id_name;            // fully scoped name of this node
    uint32_t serialized_length;     // length of this node when serialized to output file
    std::vector<Node> children;     // child nodes (for lists and arrays)

public:
    uint64_t int_value;             // used for integer and boolean types
    std::string string_value;       // string representation of this node

public:
    Node(mdi_id_t id, std::string name);


    inline mdi_id_t get_id() const { return id; }
    inline const char* get_id_name() const { return id_name.c_str(); }
    inline mdi_type_t get_type() const { return MDI_ID_TYPE(id); };

    inline void add_child(const Node& node) {
        children.push_back(node);
    }

    inline void print() {
        print(0, false);
    }

    void compute_node_length();
    bool serialize(std::ofstream& out_file);

private:
    void print_indent(int depth);
    void print_children(int depth, bool in_array);
    void print(int depth, bool in_array);
    void compute_array_length();
};
