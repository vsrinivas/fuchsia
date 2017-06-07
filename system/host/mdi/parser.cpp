// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <string.h>
#include <stdio.h>

#include <ctime>
#include <map>

#include "node.h"
#include "parser.h"

//#define PRINT_ID_DECLARATIONS 1

static bool parse_node(Tokenizer& tokenizer, Token& token, Node& parent);

// map of identifier names to mdi_id_t
static std::map<std::string, mdi_id_t> id_map;

// map of ID numbers to identifier names
static std::map<uint32_t, std::string> id_name_map;

// map of ID numbers to C symbol names
static std::map<uint32_t, std::string> id_c_name_map;

// map of C symbol names to ID numbers
static std::map<std::string, uint32_t> c_name_id_map;

static bool find_node_id(Tokenizer& tokenizer, std::string id_name, mdi_id_t& out_id) {
    const char* name_str = id_name.c_str();

    // start searching with fully scoped name
    while (name_str) {
        auto iter = id_map.find(name_str);
        if (iter != id_map.end()) {
            out_id = iter->second;
            return true;
        }
        // skip outermost scope
        name_str = strchr(name_str, '.');
        if (name_str) {
            // skip the dot
            name_str++;
        }
    }

    tokenizer.print_err("undefined identifier \"%s\"\n", id_name.c_str());
    return false;
}

static bool parse_id_declaration(Tokenizer& tokenizer, mdi_type_t type) {
    mdi_type_t element_type = MDI_INVALID_TYPE;

    if (type == MDI_ARRAY) {
        // array declarations are followed by child type
        Token token;
        if (!tokenizer.next_token(token)) {
            return false;
        }
        if (token.type == TOKEN_EOF) {
            tokenizer.print_err("end of file while parsing ID declaration\n");
            return false;
        }
        if (token.type != TOKEN_ARRAY_START) {
            tokenizer.print_err("expected \'[' after \"array\"\n");
            return false;
        }
        if (!tokenizer.next_token(token)) {
            return false;
        }
        if (token.type == TOKEN_EOF) {
            tokenizer.print_err("end of file while parsing ID declaration\n");
            return false;
        }

        element_type = token.get_type_name();
        switch (element_type) {
        case MDI_UINT8:
        case MDI_INT32:
        case MDI_UINT32:
        case MDI_UINT64:
        case MDI_BOOLEAN:
            break;
        default:
            tokenizer.print_err("Arrays of type %s are not supported\n",
                                token.string_value.c_str());
            return false;
        }

        if (!tokenizer.next_token(token)) {
            return false;
        }
        if (token.type == TOKEN_EOF) {
            tokenizer.print_err("end of file while parsing ID declaration\n");
            return false;
        }
        if (token.type != TOKEN_ARRAY_END) {
            tokenizer.print_err("expected \'[' after array child type\n");
            return false;
        }
    }

    // build id_name from string of TOKEN_IDENTIFIER and TOKEN_DOT tokens
    std::string id_name;
    Token token;

    while (1) {
        // Expecting TOKEN_IDENTIFIER
        if (!tokenizer.next_token(token)) {
            return false;
        }
        if (token.type == TOKEN_EOF) {
            tokenizer.print_err("end of file while parsing ID declaration\n");
            return false;
        }
        if (token.type != TOKEN_IDENTIFIER) {
            tokenizer.print_err("expected identifier, got token \"%s\" in ID declaration\n",
                                token.string_value.c_str());
            return false;
        }
        id_name += token.string_value;

        // Expecting TOKEN_INT_LITERAL or TOKEN_DOT
        if (!tokenizer.next_token(token)) {
            return false;
        }
        if (token.type == TOKEN_EOF) {
            tokenizer.print_err("end of file while parsing ID declaration\n");
            return false;
        }
        if (token.type == TOKEN_DOT) {
            id_name += '.';
        } else {
            break;
        }
    }

    if (token.type != TOKEN_IDENTIFIER) {
        tokenizer.print_err("Expected identifier for C symbol name, got token \"%s\" "
                            "in ID declaration for \"%s\"\n",
                            token.string_value.c_str(), id_name.c_str());
        return false;
    }

    std::string c_name = token.string_value.c_str();
    if (c_name_id_map.find(c_name) != c_name_id_map.end()) {
        tokenizer.print_err("duplicate C symbol %s\n", c_name.c_str());
        return false;
    }
    // the parser will almost verify that c_name is a legal C symbol.
    // just need to check that it does not contain any dashes.
    // we are not bothering to check for C/C++ reserved words.
    if (strchr(c_name.c_str(), '-') != nullptr) {
        tokenizer.print_err("Illegal C identifier %s\n", c_name.c_str());
        return false;
    }

    if (!tokenizer.next_token(token)) {
        return false;
    }
    if (token.type != TOKEN_INT_LITERAL) {
        tokenizer.print_err("expected integer ID, got token \"%s\" in ID declaration for \"%s\"\n",
                            token.string_value.c_str(), id_name.c_str());
        return false;
    }

    if (id_map.find(id_name) != id_map.end()) {
        tokenizer.print_err("duplicate declaration for ID %s\n", id_name.c_str());
        return false;
    }

    if (token.int_value < 1 || token.int_value > MDI_MAX_ID) {
        tokenizer.print_err("ID number %" PRId64 " for ID %s out of range\n",
                            token.int_value, id_name.c_str());
    }
    uint64_t id_number = token.int_value;
    auto duplicate = id_name_map.find(id_number);
    if (duplicate != id_name_map.end()) {
        tokenizer.print_err("ID number %" PRId64 " has already been assigned to ID %s\n",
                            id_number, duplicate->second.c_str());
        return false;
    }

    mdi_id_t id;
    if (element_type == MDI_INVALID_TYPE) {
        id = MDI_MAKE_ID(type, id_number);
    } else {
        id = MDI_MAKE_ARRAY_ID(element_type, id_number);
    }
    id_map[id_name] = id;
    id_name_map[id_number] = id_name;
    c_name_id_map[c_name] = id;
    id_c_name_map[id] = c_name;

#if PRINT_ID_DECLARATIONS
    printf("ID %s : %08X\n", name, id);
#endif
    return true;
}

static bool parse_include(Tokenizer& tokenizer, Node& root) {
    Token token;

    if (!tokenizer.next_token(token)) {
        return false;
    }
    if (token.type == TOKEN_EOF) {
        tokenizer.print_err("end of file while parsing ID declaration\n");
        return false;
    }
    if (token.type != TOKEN_STRING_LITERAL) {
        tokenizer.print_err("expected string file path after include, got \"%s\"\n",
                token.string_value.c_str());
        return false;
    }

    return process_file(&tokenizer, token.string_value.c_str(), root);
}

static bool parse_int_node(Tokenizer& tokenizer, Node& node, Token& token, Node& parent) {
    if (token.type != TOKEN_INT_LITERAL && token.type != TOKEN_NEG_INT_LITERAL) {
        tokenizer.print_err("expected integer value for node \"%s\", got \"%s\"\n",
                node.get_id_name(), token.string_value.c_str());
        return false;
    }

    mdi_type_t type = node.get_type();

    // signed version of our value
    int64_t value = (int64_t)token.int_value;
    if (token.type == TOKEN_NEG_INT_LITERAL) {
        value = -value;
    }

    switch (type) {
    case MDI_UINT8:
        if (value > UINT8_MAX || value < 0) goto out_of_range;
        node.int_value = value;
        break;
    case MDI_INT32:
        if (value > INT32_MAX || value < INT32_MIN) goto out_of_range;
        node.int_value = value;
        break;
    case MDI_UINT32:
        if (value > UINT32_MAX || value < 0) goto out_of_range;
        node.int_value = value;
        break;
    case MDI_UINT64:
        node.int_value = token.int_value;
        break;
    default:
        assert(0);
        return false;
    }

    parent.add_child(node);
    return true;

out_of_range:
    tokenizer.print_err("integer value %s%" PRId64 " out of range for \"%s\"\n",
                        (token.type == TOKEN_NEG_INT_LITERAL ? "-" : ""),
                        token.int_value, node.get_id_name());
    return false;
}

static bool parse_string_node(Tokenizer& tokenizer, Node& node, Token& token, Node& parent) {
     if (token.type != TOKEN_STRING_LITERAL) {
        tokenizer.print_err("expected string value for node \"%s\", got \"%s\"\n", node.get_id_name(),
                            token.string_value.c_str());
        return false;
    }

    node.string_value = token.string_value;
    parent.add_child(node);

    return true;
}

static bool parse_boolean_node(Tokenizer& tokenizer, Node& node, Token& token, Node& parent) {
    if (token.type == TOKEN_TRUE) {
        node.int_value = 1;
    } else if (token.type == TOKEN_FALSE) {
        node.int_value = 0;
    } else {
        tokenizer.print_err("expected boolean value for node \"%s\", got \"%s\"\n", node.get_id_name(),
                            token.string_value.c_str());
        return false;
    }

    parent.add_child(node);
    return true;
}

static bool parse_list_node(Tokenizer& tokenizer, Node& node, Token& token, Node& parent) {
    if (token.type != TOKEN_LIST_START) {
        tokenizer.print_err("expected list value for node \"%s\", got \"%s\"\n", node.get_id_name(),
                            token.string_value.c_str());
        return false;
    }

    while (1) {
        Token token;
        if (!tokenizer.next_token(token)) {
            return false;
        }
        if (token.type == TOKEN_EOF) {
            tokenizer.print_err("end of file while parsing list children\n");
            return false;
        } else if (token.type == TOKEN_LIST_END) {
            break;
        }

        if (!parse_node(tokenizer, token, node)) {
            return false;
        }
    }

    parent.add_child(node);
    return true;
}

static bool parse_array_node(Tokenizer& tokenizer, Node& node, Token& token, Node& parent) {
    if (token.type != TOKEN_ARRAY_START) {
        tokenizer.print_err("expected array value for node \"%s\", got \"%s\"\n",
                            node.get_id_name(), token.string_value.c_str());
        return false;
    }
    mdi_type_t element_type = MDI_ID_ARRAY_TYPE(node.get_id());
    mdi_id_t element_id = MDI_MAKE_ID(element_type, 0);

    while (1) {
        Token token;
        if (!tokenizer.next_token(token)) {
            return false;
        }
        if (token.type == TOKEN_EOF) {
            tokenizer.print_err("end of file while parsing list children\n");
            return false;
        } else if (token.type == TOKEN_ARRAY_END) {
            break;
        }

        Node element_node(element_id, node.get_id_name());

        switch (element_type) {
        case MDI_UINT8:
        case MDI_INT32:
        case MDI_UINT32:
        case MDI_UINT64:
            if (!parse_int_node(tokenizer, element_node, token, node)) {
                return false;
            }
            break;
        case MDI_BOOLEAN:
            if (!parse_boolean_node(tokenizer, element_node, token, node)) {
                return false;
            }
            break;
        default:
            assert(0);
            break;
        }
    }

    parent.add_child(node);
    return true;
}

static bool parse_node(Tokenizer& tokenizer, Token& token, Node& parent) {
    mdi_id_t id;

    // handle anonymous list nodes
    if (token.type == TOKEN_LIST_START) {
        id = MDI_MAKE_ID(MDI_LIST, 0);
        Node node(id, parent.get_id_name());
        return parse_list_node(tokenizer, node, token, parent);
    } else if (token.type != TOKEN_IDENTIFIER) {
        tokenizer.print_err("expected identifier or \'{\', got \"%s\"\n", token.string_value.c_str());
        return false;
    }

    std::string id_name;
    if (strlen(parent.get_id_name()) == 0) {
        id_name = token.string_value;
    } else {
        id_name = parent.get_id_name();
        id_name += ".";
        id_name += token.string_value;
    }
    if (!find_node_id(tokenizer, id_name, id)) {
        return false;
    }
    Node node(id, id_name);

    Token equals_token;
    if (!tokenizer.next_token(equals_token)) {
        return false;
    }
    if (equals_token.type != TOKEN_EQUALS) {
        tokenizer.print_err("expected \'=\' after identifier %s\n", token.string_value.c_str());
        return false;
    }

    Token value;
    if (!tokenizer.next_token(value)) {
        return false;
    }
    if (value.type == TOKEN_EOF) {
        tokenizer.print_err("end of file while parsing node\n");
        return false;
    }

    switch (MDI_ID_TYPE(id)) {
    case MDI_LIST:
        return parse_list_node(tokenizer, node, value, parent);
    case MDI_UINT8:
    case MDI_INT32:
    case MDI_UINT32:
    case MDI_UINT64:
        return parse_int_node(tokenizer, node, value, parent);
    case MDI_BOOLEAN:
        return parse_boolean_node(tokenizer, node, value, parent);
    case MDI_STRING:
        return parse_string_node(tokenizer, node, value, parent);
    case MDI_ARRAY:
        return parse_array_node(tokenizer, node, value, parent);
    default:
        tokenizer.print_err("internal error: Unknown type %d\n", MDI_ID_TYPE(id));
        return false;
    }
}

bool process_file(Tokenizer* container, const char* in_path, Node& root) {
    Tokenizer tokenizer;
    if (!tokenizer.open_file(container, in_path)) {
        return false;
    }

    while (1) {
        Token token;

        if (!tokenizer.next_token(token)) {
            return false;
        }
        if (token.type == TOKEN_EOF) {
            // on to the next input file
            break;
        }

        // ID declarations start with a type name
        mdi_type_t type = token.get_type_name();
        if (type != MDI_INVALID_TYPE) {
            if (!parse_id_declaration(tokenizer, type)) {
                return false;
            }
        } else if (token.type == TOKEN_INCLUDE) {
            if (!parse_include(tokenizer, root)) {
                return false;
            }
        } else if (token.type == TOKEN_IDENTIFIER) {
            if (!parse_node(tokenizer, token, root)) {
                return false;
            }
        } else {
            tokenizer.print_err("unexpected token \"%s\" at top level\n",
                                token.string_value.c_str());
            return false;
        }
    }

    return true;
}

constexpr char kAuthors[] = "The Fuchsia Authors";

bool generate_file_header(std::ofstream& os) {
    auto t = std::time(nullptr);
    auto ltime = std::localtime(&t);

    os << "// Copyright " << ltime->tm_year + 1900
       << " " << kAuthors << ". All rights reserved.\n";
    os << "// This is a GENERATED file. The license governing this file can be ";
    os << "found in the LICENSE file.\n\n";
    return os.good();
}

bool print_header_file(std::ofstream& os) {
    generate_file_header(os);
    for (auto iter = id_c_name_map.begin(); iter != id_c_name_map.end(); iter++) {
        auto id = iter->first;
        auto symbol = iter->second.c_str();
        char buffer[1024];

        snprintf(buffer, sizeof(buffer), "#define %-50s 0x%08X\n", symbol, id);
        os << buffer;
    }

    return true;
}
