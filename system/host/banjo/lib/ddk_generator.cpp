// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "banjo/ddk_generator.h"

#include "banjo/attributes.h"
#include "banjo/names.h"

namespace banjo {

namespace {

// Various string values are looked up or computed in these
// functions. Nothing else should be dealing in string literals, or
// computing strings from these or AST values.

constexpr const char kIndent[] = "    ";

// Functions named "Emit..." are called to actually emit to an std::ostream
// is here. No other functions should directly emit to the streams.

std::ostream& operator<<(std::ostream& stream, StringView view) {
    stream.rdbuf()->sputn(view.data(), view.size());
    return stream;
}

std::string ToSnakeCase(StringView name, bool upper = false) {
    const auto is_upper = [](char c) { return c >= 'A' && c <= 'Z'; };
    const auto is_lower = [](char c) { return c >= 'a' && c <= 'z'; };
    const auto to_upper = [&](char c) { return is_lower(c) ? c + ('A' - 'a') : c; };
    const auto to_lower = [&](char c) { return is_upper(c) ? c - ('A' - 'a') : c; };
    std::string snake;
    snake += name[0];
    for (auto it = name.begin() + 1; it != name.end(); ++it) {
        if (is_upper(*it) && *(it - 1) != '_' && !is_upper(*(it - 1))) {
            snake += '_';
        }
        snake += *it;
    }
    if (upper) {
        std::transform(snake.begin(), snake.end(), snake.begin(), to_upper);
    } else {
        std::transform(snake.begin(), snake.end(), snake.begin(), to_lower);
    }
    return snake;
}

std::string ToLispCase(StringView name) {
    std::string lisp = ToSnakeCase(name);
    const auto to_lisp = [&](char c) { return c == '_' ? '-' : c; };
    std::transform(lisp.begin(), lisp.end(), lisp.begin(), to_lisp);
    return lisp;
}

std::string NameBuffer(const DdkGenerator::Member& member) {
    return member.name + (member.element_type == "void" ? "_buffer" : "_list");
}

std::string NameCount(const DdkGenerator::Member& member) {
    return member.name + (member.element_type == "void" ? "_size" : "_count");
}

bool ReturnFirst(const std::vector<DdkGenerator::Member>& output) {
    return output.size() > 0 && (output[0].kind == flat::Type::Kind::kPrimitive ||
                                 (output[0].kind == flat::Type::Kind::kIdentifier &&
                                  output[0].decl_kind == flat::Decl::Kind::kEnum));
}

void EmitFileComment(std::ostream* file, banjo::StringView name) {
    *file << "// Copyright 2018 The Fuchsia Authors. All rights reserved.\n";
    *file << "// Use of this source code is governed by a BSD-style license that can be\n";
    *file << "// found in the LICENSE file.\n\n";
    *file << "// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.\n";
    *file << "//          MODIFY system/banjo/ddk-protocol-" << name <<  "/" << name
          << ".banjo INSTEAD.\n\n";
}

void EmitHeaderGuard(std::ostream* file) {
    // TODO(704) Generate an appropriate header guard name.
    *file << "#pragma once\n";
}

void EmitIncludeHeader(std::ostream* file, StringView header) {
    *file << "#include " << header << "\n";
}

void EmitNamespacePrologue(std::ostream* file, StringView name) {
    *file << "namespace " << name << " {\n";
}

void EmitNamespaceEpilogue(std::ostream* file, StringView name) {
    *file << "} // namespace " << name << "\n";
}

void EmitBlank(std::ostream* file) {
    *file << "\n";
}

std::vector<StringView> SplitString(const std::string& src, char delimiter) {
    std::vector<StringView> result;
    if (src.empty())
        return result;

    size_t start = 0;
    while (start != std::string::npos) {
        const size_t end = src.find(delimiter, start);

        if (end == std::string::npos) {
            StringView view(&src[start], src.size() - start);
            if (!view.empty()) result.push_back(view);
            start = std::string::npos;
        } else {
            StringView view(&src[start], end - start);
            if (!view.empty()) result.push_back(view);
            start = end + 1;
        }
    }
    return result;
}

template <typename T>
void EmitDocstring(std::ostream* file, const T& decl, bool indent) {
    if (!decl.doc.empty()) {
        const auto lines = SplitString(decl.doc, '\n');
        for (auto line : lines) {
            if (indent) {
                *file << kIndent;
            }
            *file << "//" << line << "\n";
        }
    }
}

void EmitMemberDecl(std::ostream* file, const DdkGenerator::Member& member, bool output = false) {
    const auto member_name = (output ? "* " : " ") + member.name;
    switch (member.kind) {
    case flat::Type::Kind::kArray:
        *file << member.type << member_name;
        for (uint32_t array_count : member.array_counts) {
            *file << "[" << array_count << "]";
        }
        break;
    case flat::Type::Kind::kVector:
        if (output) {
            *file << member.element_type << (output ? "* " : " ") << NameBuffer(member) << ";\n"
                  << kIndent << "size_t " << NameCount(member) << ";\n"
                  << kIndent << "size_t" << member_name << "_actual";
        } else {
            const auto prefix = member.nullability == types::Nullability::kNullable ? "" : "const ";
            *file << prefix << member.element_type << (output ? "** " : "* ")
                  << NameBuffer(member) << ";\n" << kIndent << "size_t " << NameCount(member);
        }
        break;
    case flat::Type::Kind::kString:
        if (member.array_counts.size() > 0) {
            *file << "char " << member_name;
            for (uint32_t array_count : member.array_counts) {
                *file << "[" << array_count << "]";
            }
        } else {
            *file << member.type << member_name;
        }
        break;
    case flat::Type::Kind::kHandle:
    case flat::Type::Kind::kRequestHandle:
    case flat::Type::Kind::kPrimitive:
        *file << member.type << member_name;
        break;
    case flat::Type::Kind::kIdentifier:
        switch (member.decl_kind) {
        case flat::Decl::Kind::kConst:
            assert(false && "bad decl kind for member");
            break;
        case flat::Decl::Kind::kEnum:
            *file << member.type << member_name;
            break;
        case flat::Decl::Kind::kInterface:
            *file << member.type << (output ? "*" : "") << member_name;
            break;
        case flat::Decl::Kind::kStruct:
        case flat::Decl::Kind::kUnion:
            *file << member.type << member_name;
            break;
        }
        break;
    }
}

void EmitMethodInParamDecl(std::ostream* file, const DdkGenerator::Member& member,
                           bool emit_name = true) {
    const auto member_name = emit_name ? " " + member.name : "";
    switch (member.kind) {
    case flat::Type::Kind::kArray:
        *file << "const " << member.type << member_name;
        for (uint32_t array_count : member.array_counts) {
            *file << "[" << array_count << "]";
        }
        break;
    case flat::Type::Kind::kVector:
        if (emit_name) {
            *file << "const " << member.element_type << "* " << NameBuffer(member) << ", "
                  << "size_t " << NameCount(member);
        } else {
            *file << "const " << member.element_type << "*, size_t";
        }
        break;
    case flat::Type::Kind::kString:
    case flat::Type::Kind::kHandle:
    case flat::Type::Kind::kRequestHandle:
    case flat::Type::Kind::kPrimitive:
        *file << member.type << member_name;
        break;
    case flat::Type::Kind::kIdentifier:
        switch (member.decl_kind) {
        case flat::Decl::Kind::kConst:
            assert(false && "bad decl kind for member");
            break;
        case flat::Decl::Kind::kEnum:
            *file << member.type << member_name;
            break;
        case flat::Decl::Kind::kInterface:
            *file << member.type << "*" << member_name;
            break;
        case flat::Decl::Kind::kStruct:
        case flat::Decl::Kind::kUnion:
            switch (member.nullability) {
            case types::Nullability::kNullable:
                // TODO: We are using nullability as a proxy for const...
                *file << member.type << member_name;
                break;
            case types::Nullability::kNonnullable:
                *file << "const " << member.type << "*" <<  member_name;
                break;
            }
            break;
        }
        break;
    }
}

void EmitMethodOutParamDecl(std::ostream* file, const DdkGenerator::Member& member,
                            bool emit_name = true) {
    const auto member_name = emit_name ? " out_" + member.name : "";
    switch (member.kind) {
    case flat::Type::Kind::kArray:
        *file << member.type << member_name;
        for (uint32_t array_count : member.array_counts) {
            *file << "[" << array_count << "]";
        }
    case flat::Type::Kind::kVector: {
        const auto buffer_name = emit_name ? " out_" + NameBuffer(member) : "";
        const auto count_name = emit_name ? " " + NameCount(member) : "";
        const auto actual_name = emit_name ? member_name + "_actual" : "";
        switch (member.nullability) {
        case types::Nullability::kNullable:
            *file << member.element_type << "**" << buffer_name << ", "
                  << "size_t*" << count_name;
            break;
        case types::Nullability::kNonnullable:
            *file << member.element_type << "*" << buffer_name << ", "
                  << "size_t" << count_name << ", "
                  << "size_t*" << actual_name;
            break;
        }
        break;
    }
    case flat::Type::Kind::kString:
        if (emit_name) {
            *file << "char*" << member_name << ", "
                  << "size_t " << member.name << "_capacity";
        } else {
            *file << "char*, size_t";
        }
        break;
    case flat::Type::Kind::kHandle:
    case flat::Type::Kind::kRequestHandle:
    case flat::Type::Kind::kPrimitive:
        *file << member.type << "*" << member_name;
        break;
    case flat::Type::Kind::kIdentifier:
        switch (member.decl_kind) {
        case flat::Decl::Kind::kConst:
            assert(false && "bad decl kind for member");
            break;
        case flat::Decl::Kind::kEnum:
        case flat::Decl::Kind::kInterface:
        case flat::Decl::Kind::kStruct:
        case flat::Decl::Kind::kUnion:
            *file << member.type << "*" << member_name;
            break;
        }
        break;
    }
}

void EmitMethodDeclHelper(std::ostream* file, StringView method_name,
                          const std::vector<DdkGenerator::Member>& input,
                          const std::vector<DdkGenerator::Member>& output,
                          StringView ctx) {
    const bool return_first = ReturnFirst(output);
    if (return_first) {
        *file << output[0].type << " ";
    } else {
        *file << "void ";
    }
    *file << method_name << "(";
    if (!ctx.empty()) {
        *file << ctx;
    }
    bool first = ctx.empty();
    for (const auto& member : input) {
        if (first) {
            first = false;
        } else {
            *file << ", ";
        }
        EmitMethodInParamDecl(file, member);
    }
    for (auto member = output.begin() + (return_first ? 1 : 0); member != output.end();
         member++) {
        if (first) {
            first = false;
        } else {
            *file << ", ";
        }
        EmitMethodOutParamDecl(file, *member);
    }
}

void EmitProtocolMethodDecl(std::ostream* file, StringView method_name,
                            const std::vector<DdkGenerator::Member>& input,
                            const std::vector<DdkGenerator::Member>& output) {
    EmitMethodDeclHelper(file, method_name, input, output, "");
}

void EmitProtocolMethodWithCtxDecl(std::ostream* file, StringView method_name,
                                   const std::vector<DdkGenerator::Member>& input,
                                   const std::vector<DdkGenerator::Member>& output) {
    EmitMethodDeclHelper(file, method_name, input, output, "void* ctx");
}

void EmitProtocolMethodWithSpecificCtxDecl(std::ostream* file, const std::string& protocol_name,
                                           banjo::StringView method_name,
                                           const std::vector<DdkGenerator::Member>& input,
                                           const std::vector<DdkGenerator::Member>& output) {
    EmitMethodDeclHelper(file, method_name, input, output,
                         "const " + protocol_name + "_t* proto");
}

void EmitProtocolMethodPtrDecl(std::ostream* file, const std::string& method_name,
                               const std::vector<DdkGenerator::Member>& input,
                               const std::vector<DdkGenerator::Member>& output) {
    EmitMethodDeclHelper(file, "(*" + method_name + ")", input, output, "void* ctx");
}

void EmitProtocolMethodTemplateDecl(std::ostream* file,
                                    const std::vector<DdkGenerator::Member>& input,
                                    const std::vector<DdkGenerator::Member>& output) {
    EmitMethodDeclHelper(file, "(C::*)", input, output, "");
    *file << "));\n";
}

void EmitMethodImplHelper(std::ostream* file, StringView method_name,
                          const std::vector<DdkGenerator::Member>& input,
                          const std::vector<DdkGenerator::Member>& output,
                          StringView ctx, bool save_ret=false) {
    const bool return_first = ReturnFirst(output);
    if (return_first)
        *file << (save_ret ? "auto ret = " : "return ");
    *file << method_name << "(";

    if (!ctx.empty()) {
        *file << ctx;
    }
    bool first = ctx.empty();
    for (const auto& member : input) {
        if (first) {
            first = false;
        } else {
            *file << ", ";
        }
        if (member.kind == flat::Type::Kind::kVector) {
            *file << NameBuffer(member) << ", " << NameCount(member);
        } else {
            *file << member.name;
        }
    }
    for (auto member = output.begin() + (return_first ? 1 : 0); member != output.end();
         member++) {
        if (first) {
            first = false;
        } else {
            *file << ", ";
        }

        if (member->kind == flat::Type::Kind::kVector) {
            *file << "out_" << NameBuffer(*member) << ", " << NameCount(*member);
            if (member->nullability == types::Nullability::kNonnullable) {
                *file << ", out_" << member->name << "_actual";
            }
        } else if (member->kind == flat::Type::Kind::kString) {
            *file << "out_" << member->name << ", " << member->name << "_capacity";
        } else {
            *file << (member->address_of ? "&" : "") << "out_" << member->name;
        }
    }
}

void EmitDdkProtocolMethodImpl(std::ostream* file, const std::string& method_name,
                               const std::vector<DdkGenerator::Member>& input,
                               const std::vector<DdkGenerator::Member>& output) {
    EmitMethodImplHelper(file,  "proto->ops->" + method_name, input, output,
                         "proto->ctx");
    *file << ");\n";
}

void EmitDdktlProtocolMethodImpl(std::ostream* file, const std::string& method_name,
                                 std::vector<DdkGenerator::Member> input,
                                 std::vector<DdkGenerator::Member> output,
                                 bool handle_wrappers) {
    if (handle_wrappers) {
        for (auto& member : input) {
            if (member.kind == flat::Type::Kind::kHandle) {
                member.name = member.type + "(" + member.name + ")";
            }
        }
        for (auto& member : output) {
            if (member.kind == flat::Type::Kind::kHandle) {
                *file << kIndent << kIndent <<  member.type << " out_" << member.name << "2;\n";
                member.name = member.name + "2";
                member.address_of = true;
            }
        }
        *file << kIndent << kIndent;
        EmitMethodImplHelper(file, "static_cast<D*>(ctx)->" + method_name, input, output, "", true);
        *file << ");\n";
        for (auto& member : output) {
            if (member.kind == flat::Type::Kind::kHandle) {
                *file << kIndent << kIndent <<  "*out_"
                      << member.name.substr(0, member.name.size() - 1) << " = out_"
                      << member.name << ".release();\n";
            }
        }
        if (ReturnFirst(output)) {
            *file << kIndent << kIndent << "return ret;\n";
        }
    } else {
        *file << kIndent << kIndent;
        EmitMethodImplHelper(file, "static_cast<D*>(ctx)->" + method_name, input, output, "");
        *file << ");\n";
    }
}

void EmitClientMethodImpl(std::ostream* file, const std::string& method_name,
                           std::vector<DdkGenerator::Member>& input,
                          std::vector<DdkGenerator::Member>& output,
                          bool handle_wrappers) {
    if (handle_wrappers) {
        for (auto& member : input) {
            if (member.kind == flat::Type::Kind::kHandle) {
                member.name = member.name + ".release()";
            }
        }
        for (auto& member : output) {
            if (member.kind == flat::Type::Kind::kHandle) {
                member.name = member.name + "->reset_and_get_address()";
            }
        }
    }
    EmitMethodImplHelper(file, "ops_->" + method_name, input, output, "ctx_");
    *file << ");\n";
}

void EmitCallbackMethodImpl(std::ostream* file, const std::string& method_name,
                            const std::vector<DdkGenerator::Member>& members) {
    *file << kIndent << "struct " << method_name << "_callback_context* ctx = cookie;\n";
    EmitBlank(file);
    for (const auto& member : members) {
        const auto& name = member.name;
        switch (member.kind) {
        case flat::Type::Kind::kArray:
            *file << kIndent << "memcpy(ctx->" << name << ", " << name << ", sizeof(" << name << "));\n";
            break;
        case flat::Type::Kind::kVector:
            *file << kIndent << "memcpy(ctx->" << NameBuffer(member) << ", " << NameBuffer(member)
                  << ", sizeof(*" << NameBuffer(member) <<  ") * " << NameCount(member) << ");\n";
            *file << kIndent << "*ctx->" << name << "_actual = " << NameCount(member) << ";\n";
            break;
        case flat::Type::Kind::kString:
            *file << kIndent << "strcpy(ctx->" << name << ", " << name << ");\n";
            break;
        case flat::Type::Kind::kHandle:
        case flat::Type::Kind::kRequestHandle:
        case flat::Type::Kind::kPrimitive:
            *file << kIndent << "*ctx->" << name << " = " << name << ";\n";
            break;
        case flat::Type::Kind::kIdentifier:
            switch (member.decl_kind) {
            case flat::Decl::Kind::kConst:
                assert(false && "bad decl kind for member");
                break;
            case flat::Decl::Kind::kEnum:
            case flat::Decl::Kind::kInterface:
                *file << kIndent << "*ctx->" << name << " = " << name << ";\n";
                break;
            case flat::Decl::Kind::kStruct:
            case flat::Decl::Kind::kUnion:
                switch (member.nullability) {
                case types::Nullability::kNullable:
                    *file << kIndent << "if (" << name << ") {\n";
                    *file << kIndent << kIndent << "*ctx->" << name << " = *" << name << ";\n";
                    *file << kIndent << "} else {\n";
                    // We don't have a great way of signaling that the optional response member
                    // was not in the message. That means these bindings aren't particularly
                    // useful when the client needs to extract that bit. The best we can do is
                    // zero out the value to make sure the client has defined behavior.
                    //
                    // In many cases, the response contains other information (e.g., a status code)
                    // that lets the client do something reasonable.
                    *file << kIndent << kIndent << "memset(ctx->" << name
                          << ", 0, sizeof(*ctx->" << name << "));\n";
                    *file << kIndent << "}\n";
                    break;
                case types::Nullability::kNonnullable:
                    *file << kIndent << "*ctx->" << name << " = *" << name << ";\n";
                    break;
                }
                break;
            }
            break;
        }
    }
    EmitBlank(file);
    *file << kIndent << "sync_completion_signal(&ctx->completion);\n";
}

void EmitSyncMethodImpl(std::ostream* file, const std::string& protocol_name,
                        const std::string& method_name,
                        const std::vector<DdkGenerator::Member>& input,
                        const std::vector<DdkGenerator::Member>& members) {
    *file << kIndent << "struct " << method_name << "_callback_context ctx;\n";
    *file << kIndent << "sync_completion_reset(&ctx.completion);\n";

    const bool return_first = ReturnFirst(members);
    if (return_first) {
        *file << kIndent << members[0].type << " _" << members[0].name << ";\n";
        *file << kIndent << members[0].type << "* out_" << members[0].name << " = &_"
              << members[0].name << ";\n";
    }
    EmitBlank(file);
    for (const auto& member : members) {
        const auto& name = member.name;
        switch (member.kind) {
        case flat::Type::Kind::kArray:
            *file << kIndent << "ctx." << name << " = out_" << name << "\n";
            break;
        case flat::Type::Kind::kVector:
            *file << kIndent << "ctx." << NameBuffer(member) << " = out_" << NameBuffer(member)
                  << ";\n";
            *file << kIndent << "ctx." << NameCount(member) << " = " << NameCount(member) << ";\n";
            *file << kIndent << "ctx." << name << "_actual = out_" << name << "_actual;\n";
            break;
        case flat::Type::Kind::kString:
            *file << kIndent << "ctx." << name << " = out_" << name << ";\n";
            *file << kIndent << "ctx." << name << "capacity = out_" << name << "capacity;\n";
            break;
        case flat::Type::Kind::kHandle:
        case flat::Type::Kind::kRequestHandle:
        case flat::Type::Kind::kPrimitive:
            *file << kIndent << "ctx." << name << " = out_" << name << ";\n";
            break;
        case flat::Type::Kind::kIdentifier:
            switch (member.decl_kind) {
            case flat::Decl::Kind::kConst:
                assert(false && "bad decl kind for member");
                break;
            case flat::Decl::Kind::kEnum:
            case flat::Decl::Kind::kInterface:
                *file << kIndent << "ctx." << name << " = out_" << name << ";\n";
                break;
            case flat::Decl::Kind::kStruct:
            case flat::Decl::Kind::kUnion:
                switch (member.nullability) {
                case types::Nullability::kNullable:
                    *file << kIndent << "ctx." << name << " = out_" << name << ";\n";
                    break;
                case types::Nullability::kNonnullable:
                    *file << kIndent << "ctx." << name << " = out_" << name << ";\n";
                    break;
                }
                break;
            }
            break;
        }
    }

    EmitBlank(file);
    *file << kIndent;
    EmitMethodImplHelper(file, method_name, input, {}, protocol_name);
    *file << ", " << method_name << "_cb, &ctx);\n";
    *file << kIndent
          << "zx_status_t status = sync_completion_wait(&ctx.completion, ZX_TIME_INFINITE);\n";
    if (return_first) {
        *file << kIndent << "if (status != ZX_OK) {\n";
        *file << kIndent << kIndent << "return status;\n";
        *file << kIndent << "}\n";
        *file << kIndent << "return _" << members[0].name << ";\n";
    } else {
        *file << kIndent << "assert(status == ZX_OK);\n";
    }
}

// Various computational helper routines.

void EnumValue(types::PrimitiveSubtype type, const flat::Constant* constant,
               const flat::Library* library, std::string* out_value) {
    std::ostringstream member_value;

    switch (type) {
    case types::PrimitiveSubtype::kInt8: {
        int8_t value;
        bool success = library->ParseIntegerConstant(constant, &value);
        if (!success) {
            __builtin_trap();
        }
        // The char-sized overloads of operator<< here print
        // the character value, not the numeric value, so cast up.
        member_value << static_cast<int>(value);
        break;
    }
    case types::PrimitiveSubtype::kInt16: {
        int16_t value;
        bool success = library->ParseIntegerConstant(constant, &value);
        if (!success) {
            __builtin_trap();
        }
        member_value << value;
        break;
    }
    case types::PrimitiveSubtype::kInt32: {
        int32_t value;
        bool success = library->ParseIntegerConstant(constant, &value);
        if (!success) {
            __builtin_trap();
        }
        member_value << value;
        break;
    }
    case types::PrimitiveSubtype::kInt64: {
        int64_t value;
        bool success = library->ParseIntegerConstant(constant, &value);
        if (!success) {
            __builtin_trap();
        }
        member_value << value;
        break;
    }
    case types::PrimitiveSubtype::kUint8: {
        uint8_t value;
        bool success = library->ParseIntegerConstant(constant, &value);
        if (!success) {
            __builtin_trap();
        }
        // The char-sized overloads of operator<< here print
        // the character value, not the numeric value, so cast up.
        member_value << static_cast<unsigned int>(value);
        break;
    }
    case types::PrimitiveSubtype::kUint16: {
        uint16_t value;
        bool success = library->ParseIntegerConstant(constant, &value);
        if (!success) {
            __builtin_trap();
        }
        member_value << value;
        break;
    }
    case types::PrimitiveSubtype::kUint32: {
        uint32_t value;
        bool success = library->ParseIntegerConstant(constant, &value);
        if (!success) {
            __builtin_trap();
        }
        member_value << value;
        break;
    }
    case types::PrimitiveSubtype::kUint64: {
        uint64_t value;
        bool success = library->ParseIntegerConstant(constant, &value);
        if (!success) {
            __builtin_trap();
        }
        member_value << value;
        break;
    }
    case types::PrimitiveSubtype::kBool:
    case types::PrimitiveSubtype::kFloat32:
    case types::PrimitiveSubtype::kFloat64:
    case types::PrimitiveSubtype::kUSize:
    case types::PrimitiveSubtype::kISize:
    case types::PrimitiveSubtype::kVoidPtr:
        assert(false && "bad primitive type for an enum");
        break;
    }

    *out_value = member_value.str();
}

std::vector<uint32_t> ArrayCounts(const flat::Library* library, const flat::Type* type) {
    std::vector<uint32_t> array_counts;
    for (;;) {
        switch (type->kind) {
        default: { return array_counts; }
        case flat::Type::Kind::kArray: {
            auto array_type = static_cast<const flat::ArrayType*>(type);
            uint32_t element_count = array_type->element_count.Value();
            array_counts.push_back(element_count);
            type = array_type->element_type.get();
            continue;
        }
        case flat::Type::Kind::kString: {
            auto str_type = static_cast<const flat::StringType*>(type);
            uint32_t max_size = str_type->max_size.Value();
            if (max_size < flat::Size::Max().Value())
                array_counts.push_back(max_size);
            return array_counts;
        }
        }
    }
}

flat::Decl::Kind GetDeclKind(const flat::Library* library, const flat::Type* type) {
    if (type->kind != flat::Type::Kind::kIdentifier)
        return flat::Decl::Kind::kConst;
    auto identifier_type = static_cast<const flat::IdentifierType*>(type);
    auto named_decl = library->LookupDeclByName(identifier_type->name);
    assert(named_decl && "library must contain declaration");
    return named_decl->kind;
}

std::string HandleToZxWrapper(const flat::HandleType* handle_type) {
    switch (handle_type->subtype) {
    case types::HandleSubtype::kHandle:
        return "zx::handle";
    case types::HandleSubtype::kProcess:
        return "zx::process";
    case types::HandleSubtype::kThread:
        return "zx::thread";
    case types::HandleSubtype::kVmo:
        return "zx::vmo";
    case types::HandleSubtype::kChannel:
        return "zx::channel";
    case types::HandleSubtype::kEvent:
        return "zx::event";
    case types::HandleSubtype::kPort:
        return "zx::port";
    case types::HandleSubtype::kInterrupt:
        return "zx::interrupt";
    case types::HandleSubtype::kLog:
        return "zx::debuglog";
    case types::HandleSubtype::kSocket:
        return "zx::socket";
    case types::HandleSubtype::kResource:
        return "zx::resource";
    case types::HandleSubtype::kEventpair:
        return "zx::eventpair";
    case types::HandleSubtype::kJob:
        return "zx::job";
    case types::HandleSubtype::kVmar:
        return "zx::vmar";
    case types::HandleSubtype::kFifo:
        return "zx::fifo";
    case types::HandleSubtype::kGuest:
        return "zx::guest";
    case types::HandleSubtype::kTimer:
        return "zx::timer";
    case types::HandleSubtype::kBti:
        return "zx::bti";
    case types::HandleSubtype::kProfile:
        return "zx::profile";
    default: { abort(); }
    }
}

std::string NameType(const flat::Type* type, const flat::Decl::Kind& decl_kind,
                     bool handle_wrappers=false) {
    for (;;) {
        switch (type->kind) {
        case flat::Type::Kind::kHandle:
        case flat::Type::Kind::kRequestHandle: {
            if (handle_wrappers) {
                auto handle_type = static_cast<const flat::HandleType*>(type);
                return HandleToZxWrapper(handle_type);
            } else {
                return "zx_handle_t";
            }
        }

        case flat::Type::Kind::kString:
            return "const char*";

        case flat::Type::Kind::kPrimitive: {
            auto primitive_type = static_cast<const flat::PrimitiveType*>(type);
            if (primitive_type->subtype == types::PrimitiveSubtype::kInt32)
                return "zx_status_t";
            return NamePrimitiveCType(primitive_type->subtype);
        }

        case flat::Type::Kind::kArray: {
            auto array_type = static_cast<const flat::ArrayType*>(type);
            type = array_type->element_type.get();
            continue;
        }

        case flat::Type::Kind::kVector: {
            auto vector_type = static_cast<const flat::VectorType*>(type);
            type = vector_type->element_type.get();
            continue;
        }

        case flat::Type::Kind::kIdentifier: {
            auto identifier_type = static_cast<const flat::IdentifierType*>(type);
            switch (decl_kind) {
            case flat::Decl::Kind::kConst:
            case flat::Decl::Kind::kEnum:
            case flat::Decl::Kind::kStruct:
            case flat::Decl::Kind::kUnion: {
                std::string name = identifier_type->name.name().data();
                name = ToSnakeCase(name) + "_t";
                if (identifier_type->nullability == types::Nullability::kNullable) {
                    name.push_back('*');
                }
                return name;
            }
            case flat::Decl::Kind::kInterface: {
                return std::string(
                    "const " + ToSnakeCase(identifier_type->name.name().data()) + "_t");
            }
            default: { abort(); }
            }
        }
        default: { abort(); }
        }
    }
}

template <typename T>
DdkGenerator::Member CreateMember(const flat::Library* library, const T& decl,
                                  bool handle_wrappers=false) {
    std::string name = NameIdentifier(decl.name);
    const flat::Type* type = decl.type.get();
    auto decl_kind = GetDeclKind(library, type);
    auto type_name = NameType(type, decl_kind, handle_wrappers);
    std::vector<uint32_t> array_counts = ArrayCounts(library, type);
    std::string element_type_name;
    std::string doc = decl.GetAttribute("Doc");
    if (type->kind == flat::Type::Kind::kVector) {
        auto vector_type = static_cast<const flat::VectorType*>(type);
        const flat::Type* element_type = vector_type->element_type.get();
        element_type_name = NameType(element_type, GetDeclKind(library, element_type));
    }
    types::Nullability nullability = types::Nullability::kNonnullable;
    if (type->kind == flat::Type::Kind::kIdentifier) {
        auto identifier_type = static_cast<const flat::IdentifierType*>(type);
        nullability = identifier_type->nullability;
    } else if (type->kind == flat::Type::Kind::kVector) {
        auto identifier_type = static_cast<const flat::VectorType*>(type);
        nullability = identifier_type->nullability;
    }
    return DdkGenerator::Member{
        type->kind,
        decl_kind,
        std::move(type_name),
        std::move(name),
        std::move(element_type_name),
        std::move(doc),
        std::move(array_counts),
        nullability,
    };
}

template <typename T>
std::vector<DdkGenerator::Member>
GenerateMembers(const flat::Library* library, const std::vector<T>& decl_members) {
    std::vector<DdkGenerator::Member> members;
    members.reserve(decl_members.size());
    for (const auto& member : decl_members) {
        members.push_back(CreateMember(library, member, false));
    }
    return members;
}

void GetMethodParameters(const flat::Library* library,
                         const DdkGenerator::NamedMethod& method_info,
                         std::vector<DdkGenerator::Member>* input,
                         std::vector<DdkGenerator::Member>* output,
                         bool handle_wrappers=false) {
    input->reserve(method_info.input_parameters.size() + (method_info.async ? 2 : 0));
    for (const auto& parameter : method_info.input_parameters) {
        input->push_back(CreateMember(library, parameter, handle_wrappers));
    }

    if (method_info.async) {
        input->push_back(DdkGenerator::Member{
            .kind = flat::Type::Kind::kIdentifier,
            .decl_kind = flat::Decl::Kind::kStruct,
            .type = ToSnakeCase(method_info.protocol_name) + "_callback",
            .name = "callback",
            .element_type = "",
            .array_counts = {},
            .nullability = types::Nullability::kNullable,
        });
        input->push_back(DdkGenerator::Member{
            .kind = flat::Type::Kind::kPrimitive,
            .decl_kind = flat::Decl::Kind::kStruct,
            .type = "void*",
            .name = "cookie",
            .element_type = "",
            .array_counts = {},
            .nullability = types::Nullability::kNullable,
        });
    } else {
        output->reserve(method_info.output_parameters.size());
        for (const auto& parameter : method_info.output_parameters) {
            output->push_back(CreateMember(library, parameter, handle_wrappers));
        }
    }
}
} // namespace

void DdkGenerator::GeneratePrologues() {
    EmitFileComment(&file_, library_->name().back());
    EmitHeaderGuard(&file_);
    EmitBlank(&file_);

    for (const auto& dep_library : library_->dependencies()) {
        if (dep_library == library_)
            continue;
        if (dep_library->HasAttribute("Internal"))
            continue;
        EmitIncludeHeader(&file_, "<" +  ToLispCase(StringJoin(dep_library->name(), "/")) + ".h>");
    }
    EmitIncludeHeader(&file_, "<zircon/compiler.h>");
    EmitIncludeHeader(&file_, "<zircon/types.h>");

    EmitBlank(&file_);
    file_ <<  "__BEGIN_CDECLS;\n";
}

void DdktlGenerator::GeneratePrologues() {
    EmitFileComment(&file_, library_->name().back());
    EmitHeaderGuard(&file_);
    EmitBlank(&file_);
    EmitIncludeHeader(&file_, "<ddk/driver.h>");
    EmitIncludeHeader(&file_, "<" +  ToLispCase(LibraryName(library_, "/")) + ".h>");
    for (const auto& dep_library : library_->dependencies()) {
        if (dep_library == library_)
            continue;
        if (dep_library->HasAttribute("Internal"))
            continue;
        EmitIncludeHeader(&file_, "<" +  ToLispCase(StringJoin(dep_library->name(), "/")) + ".h>");
    }
    EmitIncludeHeader(&file_, "<ddktl/device-internal.h>");
    EmitIncludeHeader(&file_, "<zircon/assert.h>");
    EmitIncludeHeader(&file_, "<zircon/compiler.h>");
    EmitIncludeHeader(&file_, "<zircon/types.h>");

    // Enumerate list of includes based on zx_handle_t wrappers.
    std::set<std::string> includes;

    std::map<const flat::Decl*, NamedInterface> named_interfaces =
        NameInterfaces(library_->interface_declarations_);
    for (const auto& named_interface : named_interfaces) {
        for (const auto& method_info : named_interface.second.methods) {
            std::vector<Member> input;
            std::vector<Member> output;
            GetMethodParameters(library_, method_info, &input, &output, true);

            for (const auto& member : input) {
                if (member.kind == flat::Type::Kind::kHandle) {
                    includes.insert(std::string(member.type.substr(4)));
                }
            }
            for (const auto& member : output) {
                if (member.kind == flat::Type::Kind::kHandle) {
                    includes.insert(std::string(member.type.substr(4)));
                }
            }
        }
    }

    for (const auto& include : includes) {
        EmitIncludeHeader(&file_, "<lib/zx/" + include  + ".h>");
    }

    EmitBlank(&file_);

    const auto& libname = library_->name();
    EmitIncludeHeader(&file_, "\"" + ToLispCase(libname.back()) + "-internal.h\"");
    EmitBlank(&file_);
}

void DdkGenerator::GenerateEpilogues() {
    file_ <<  "__END_CDECLS;\n";
}

void DdktlGenerator::GenerateEpilogues() {
    EmitNamespaceEpilogue(&file_, "ddk");
}

void DdkGenerator::GenerateIntegerDefine(StringView name, types::PrimitiveSubtype subtype,
                                         StringView value) {
    std::string literal_macro = NamePrimitiveIntegerCConstantMacro(subtype);
    file_ << "#define " << name << " " << literal_macro << "(" << value << ")\n";
}

void DdkGenerator::GeneratePrimitiveDefine(StringView name, types::PrimitiveSubtype subtype,
                                           StringView value) {
    switch (subtype) {
    case types::PrimitiveSubtype::kInt8:
    case types::PrimitiveSubtype::kInt16:
    case types::PrimitiveSubtype::kInt32:
    case types::PrimitiveSubtype::kInt64:
    case types::PrimitiveSubtype::kUint8:
    case types::PrimitiveSubtype::kUint16:
    case types::PrimitiveSubtype::kUint32:
    case types::PrimitiveSubtype::kUint64: {
        std::string literal_macro = NamePrimitiveIntegerCConstantMacro(subtype);
        file_ << "#define " << name << " " << literal_macro << "(" << value << ")\n";
        break;
    }
    case types::PrimitiveSubtype::kBool:
    case types::PrimitiveSubtype::kFloat32:
    case types::PrimitiveSubtype::kFloat64: {
        file_ << "#define " << name << " "
              << "(" << value << ")\n";
        break;
    }
    default:
        abort();
    }
}

void DdkGenerator::GenerateStringDefine(StringView name, StringView value) {
    file_ << "#define " << name << " " << value << "\n";
}

#if 0
void DdktlGenerator::GeneratePrimitiveDefine(StringView name, types::PrimitiveSubtype subtype,
                                             StringView value) {
    std::string underlying_type = NamePrimitiveCType(subtype);
    file_ << "constexpr " << underlying_type << " " << name << " = " << value << ";\n";
}

void DdktlGenerator::GenerateStringDefine(StringView name, StringView value) {
    file_ << "constexpr const char" << name << "[] = " << value << "\n";
}
#endif

void DdkGenerator::GenerateIntegerTypedef(types::PrimitiveSubtype subtype, StringView name) {
    std::string underlying_type = NamePrimitiveCType(subtype);
    file_ << "typedef " << underlying_type << " " << name << ";\n";
}

void DdkGenerator::GenerateStructTypedef(StringView name, StringView type_name) {
    file_ << "typedef struct " << name << " " << type_name << ";\n";
}

void DdkGenerator::GenerateUnionTypedef(StringView name, StringView type_name) {
    file_ << "typedef union " << name << " " << type_name << ";\n";
}

void DdkGenerator::GenerateStructDeclaration(StringView name, const std::vector<Member>& members,
                                             bool packed, bool helper) {
    file_ << "struct " << name << " {\n";
    bool first = true;
    for (const auto& member : members) {
        if (!helper) {
            EmitDocstring(&file_, member, true);
        }
        file_ << kIndent;
        EmitMemberDecl(&file_, member, helper && !first);
        file_ << ";\n";
        if (first) first = false;
    }
    if (packed) {
        file_ << "} __attribute__((__packed__));\n";
    } else {
        file_ << "};\n";
    }
}

void DdkGenerator::GenerateTaggedUnionDeclaration(StringView name,
                                                  const std::vector<Member>& members) {
    file_ << "union " << name << " {\n";
    for (const auto& member : members) {
        EmitDocstring(&file_, member, true);
        file_ << kIndent;
        EmitMemberDecl(&file_, member);
        file_ << ";\n";
    }
    file_ << "};\n";
}

// TODO(TO-702) These should maybe check for global name
// collisions? Otherwise, is there some other way they should fail?
std::map<const flat::Decl*, DdkGenerator::NamedConst>
DdkGenerator::NameConsts(const std::vector<std::unique_ptr<flat::Const>>& const_infos) {
    std::map<const flat::Decl*, NamedConst> named_consts;
    for (const auto& const_info : const_infos) {
        std::string doc = const_info->GetAttribute("Doc");
        named_consts.emplace(const_info.get(),
                             NamedConst{NameIdentifier(const_info->name.name()), std::move(doc),
                                        *const_info});
    }
    return named_consts;
}

#if 0
std::map<const flat::Decl*, DdkGenerator::NamedConst>
DdktlGenerator::NameConsts(const std::vector<std::unique_ptr<flat::Const>>& const_infos) {
    std::map<const flat::Decl*, NamedConst> named_consts;
    for (const auto& const_info : const_infos) {
        named_consts.emplace(const_info.get(),
                             NamedConst{NameIdentifier(const_info->name.name()), *const_info});
    }
    return named_consts;
}
#endif

std::map<const flat::Decl*, DdkGenerator::NamedEnum>
DdkGenerator::NameEnums(const std::vector<std::unique_ptr<flat::Enum>>& enum_infos) {
    std::map<const flat::Decl*, NamedEnum> named_enums;
    for (const auto& enum_info : enum_infos) {
        std::string enum_name = ToSnakeCase(enum_info->name.name().data(), true);
        std::string type_name = ToSnakeCase(enum_info->name.name().data()) + "_t";
        std::string doc = enum_info->GetAttribute("Doc");
        named_enums.emplace(enum_info.get(),
                            NamedEnum{std::move(enum_name), std::move(type_name), std::move(doc),
                                      *enum_info});
    }
    return named_enums;
}

#if 0
std::map<const flat::Decl*, DdkGenerator::NamedEnum>
DdktlGenerator::NameEnums(const std::vector<std::unique_ptr<flat::Enum>>& enum_infos) {
    std::map<const flat::Decl*, NamedEnum> named_enums;
    for (const auto& enum_info : enum_infos) {
        std::string enum_name = enum_info->name.name().data();
        named_enums.emplace(enum_info.get(), NamedEnum{enum_name, enum_name, *enum_info});
    }
    return named_enums;
}
#endif

std::map<const flat::Decl*, DdkGenerator::NamedInterface>
DdkGenerator::NameInterfaces(const std::vector<std::unique_ptr<flat::Interface>>& interface_infos) {
    std::map<const flat::Decl*, NamedInterface> named_interfaces;
    for (const auto& interface_info : interface_infos) {
        const auto layout = interface_info->GetAttribute("Layout");
        InterfaceType intf_type;
        std::string name = interface_info->name.name().data();
        if (layout == "ddk-protocol") {
            name += "Protocol";
            intf_type = InterfaceType::kProtocol;
        } else if (layout == "ddk-interface") {
            intf_type = InterfaceType::kInterface;
        } else if (layout == "ddk-callback") {
            intf_type = InterfaceType::kCallback;
        } else {
            continue;
        }

        NamedInterface named_interface;
        named_interface.type = intf_type;
        named_interface.shortname = interface_info->name.name().data();
        named_interface.camel_case_name = name;
        named_interface.snake_case_name = ToSnakeCase(std::move(name));
        named_interface.doc = interface_info->GetAttribute("Doc");
        for (const auto& method_pointer : interface_info->all_methods) {
            assert(method_pointer != nullptr);
            const auto& method = *method_pointer;
            std::string c_name = ToSnakeCase(method.name.data());
            NamedMethod named_method = {
                .async = method.HasAttribute("Async"),
                .generate_sync_method = method.HasAttribute("GenerateSync"),
                .c_name = c_name,
                .protocol_name = ToSnakeCase(named_interface.shortname) + "_" + c_name,
                .proxy_name = "",
                .doc = method.GetAttribute("Doc"),
                .input_parameters = method.maybe_request->parameters,
                .output_parameters = method.maybe_response->parameters,
            };
            named_interface.methods.push_back(std::move(named_method));
        }
        named_interfaces.emplace(interface_info.get(), std::move(named_interface));
    }
    return named_interfaces;
}

std::map<const flat::Decl*, DdkGenerator::NamedInterface>
DdktlGenerator::NameInterfaces(const std::vector<std::unique_ptr<flat::Interface>>& interface_infos) {
    std::map<const flat::Decl*, NamedInterface> named_interfaces;
    for (const auto& interface_info : interface_infos) {
        const auto layout = interface_info->GetAttribute("Layout");
        InterfaceType intf_type;
        std::string name = interface_info->name.name().data();
        if (layout == "ddk-protocol") {
            name += "Protocol";
            intf_type = InterfaceType::kProtocol;
        } else if (layout == "ddk-interface") {
            intf_type = InterfaceType::kInterface;
        } else if (layout == "ddk-callback") {
            intf_type = InterfaceType::kCallback;
        } else {
            continue;
        }

        NamedInterface named_interface;
        named_interface.type = intf_type;
        named_interface.shortname = interface_info->name.name().data();
        named_interface.camel_case_name = name;
        named_interface.snake_case_name = ToSnakeCase(std::move(name));
        named_interface.doc = interface_info->GetAttribute("Doc");
        named_interface.handle_wrappers = interface_info->HasAttribute("HandleWrappers");
        for (const auto& method_pointer : interface_info->all_methods) {
            assert(method_pointer != nullptr);
            const auto& method = *method_pointer;
            std::string protocol_name = NameIdentifier(interface_info->name.name()) +
                                        NameIdentifier(method.name);
            std::string c_name = ToSnakeCase(method.name.data());
            std::string proxy_name = NameIdentifier(method.name);
            NamedMethod named_method = {
                .async = method.HasAttribute("Async"),
                .generate_sync_method = method.HasAttribute("GenerateSync"),
                .c_name = c_name,
                .protocol_name = protocol_name,
                .proxy_name = proxy_name,
                .doc = method.GetAttribute("Doc"),
                .input_parameters = method.maybe_request->parameters,
                .output_parameters = method.maybe_response->parameters,
            };
            named_interface.methods.push_back(std::move(named_method));
        }
        named_interfaces.emplace(interface_info.get(), std::move(named_interface));
    }
    return named_interfaces;
}

std::map<const flat::Decl*, DdkGenerator::NamedStruct>
DdkGenerator::NameStructs(const std::vector<std::unique_ptr<flat::Struct>>& struct_infos) {
    std::map<const flat::Decl*, NamedStruct> named_structs;
    for (const auto& struct_info : struct_infos) {
        const bool packed = struct_info->HasAttribute("Packed");
        std::string name = ToSnakeCase(struct_info->name.name().data());
        std::string type_name = name + "_t";
        std::string doc = struct_info->GetAttribute("Doc");
        named_structs.emplace(struct_info.get(),
                              NamedStruct{std::move(name), std::move(type_name), std::move(doc),
                                          packed, *struct_info});
    }
    return named_structs;
}

#if 0
std::map<const flat::Decl*, DdkGenerator::NamedStruct>
DdktlGenerator::NameStructs(const std::vector<std::unique_ptr<flat::Struct>>& struct_infos) {
    std::map<const flat::Decl*, NamedStruct> named_structs;
    for (const auto& struct_info : struct_infos) {
        const bool packed = struct_info->HasAttribute("Packed");
        if (struct_info->GetAttribute("repr") == "C") {
            std::string name = ToSnakeCase(struct_info->name.name().data());
            std::string type_name = name + "_t";
            named_structs.emplace(struct_info.get(),
                                  NamedStruct{std::move(name), std::move(type_name), packed,
                                              *struct_info});
        } else {
            const std::string name = struct_info->name.name().data();
            named_structs.emplace(struct_info.get(), NamedStruct{name, name, packed, *struct_info});
        }
    }
    return named_structs;
}
#endif

std::map<const flat::Decl*, DdkGenerator::NamedUnion>
DdkGenerator::NameUnions(const std::vector<std::unique_ptr<flat::Union>>& union_infos) {
    std::map<const flat::Decl*, NamedUnion> named_unions;
    for (const auto& union_info : union_infos) {
        std::string union_name = ToSnakeCase(union_info->name.name().data());
        std::string type_name = union_name + "_t";
        std::string doc = union_info->GetAttribute("Doc");
        named_unions.emplace(union_info.get(),
                             NamedUnion{std::move(union_name), std::move(type_name), std::move(doc),
                                        *union_info});
    }
    return named_unions;
}

#if 0
std::map<const flat::Decl*, DdkGenerator::NamedUnion>
DdktlGenerator::NameUnions(const std::vector<std::unique_ptr<flat::Union>>& union_infos) {
    std::map<const flat::Decl*, NamedUnion> named_unions;
    for (const auto& union_info : union_infos) {
        if (union_info->GetAttribute("repr") == "C") {
            std::string union_name = ToSnakeCase(union_info->name.name().data());
            std::string type_name = union_name + "_t";
            named_unions.emplace(union_info.get(),
                                 NamedUnion{std::move(union_name), std::move(type_name),
                                            *union_info});
        } else {
            const std::string union_name = union_info->name.name().data();
            named_unions.emplace(union_info.get(),
                                 NamedUnion{union_name, union_name, *union_info});
        }
    }
    return named_unions;
}
#endif

void DdkGenerator::ProduceConstForwardDeclaration(const NamedConst& named_const) {
    // TODO(TO-702)
}

void DdkGenerator::ProduceProtocolForwardDeclaration(const NamedInterface& named_interface) {
    GenerateStructTypedef(named_interface.snake_case_name, named_interface.snake_case_name + "_t");

    for (const auto& method_info : named_interface.methods) {
        if (method_info.async) {
            std::vector<Member> input;
            input.reserve(method_info.output_parameters.size());
            for (const auto& parameter : method_info.output_parameters) {
                input.push_back(CreateMember(library_, parameter));
            }

            file_ << "typedef ";
            const auto method_name = method_info.protocol_name + "_callback";
            EmitProtocolMethodPtrDecl(&file_, method_name, input, {});
            file_ << ");\n";
        }
    }
}

#if 0
void DdktlGenerator::ProduceProtocolForwardDeclaration(const NamedInterface& named_interface) {}
#endif

void DdkGenerator::ProduceEnumForwardDeclaration(const NamedEnum& named_enum) {
    types::PrimitiveSubtype subtype = named_enum.enum_info.type;
    EmitDocstring(&file_, named_enum, false);
    GenerateIntegerTypedef(subtype, named_enum.type_name);
    for (const auto& member : named_enum.enum_info.members) {
        std::string member_name = named_enum.name + "_" + NameIdentifier(member.name);
        std::string member_value;
        EnumValue(named_enum.enum_info.type, member.value.get(), library_, &member_value);
        struct {
            std::string doc;
        } temp = {member.GetAttribute("Doc")};
        EmitDocstring(&file_, temp, true);
        GenerateIntegerDefine(member_name, subtype, std::move(member_value));
    }

    EmitBlank(&file_);
}

#if 0
void DdktlGenerator::ProduceEnumForwardDeclaration(const NamedEnum& named_enum) {
    types::PrimitiveSubtype subtype = named_enum.enum_info.type;
    std::string underlying_type = NamePrimitiveCType(subtype);
    file_ << "enum class " << named_enum.name << " : " << underlying_type << " {\n";
    for (const auto& member : named_enum.enum_info.members) {
        std::string member_name = NameIdentifier(member.name);
        std::string member_value;
        EnumValue(named_enum.enum_info.type, member.value.get(), library_, &member_value);
        file_ << kIndent << member_name << " = " << member_value << ",\n";
    }
    file_ << "};\n";
}
#endif

void DdkGenerator::ProduceStructForwardDeclaration(const NamedStruct& named_struct) {
    // TODO: Hack - structs with no members are defined in a different header.
    if (named_struct.struct_info.members.empty()) return;

    GenerateStructTypedef(named_struct.name, named_struct.type_name);
}

void DdkGenerator::ProduceUnionForwardDeclaration(const NamedUnion& named_union) {
    GenerateUnionTypedef(named_union.name, named_union.type_name);
}

void DdkGenerator::ProduceConstDeclaration(const NamedConst& named_const) {
    const flat::Const& ci = named_const.const_info;

    // Some constants are not literals.  Odd.
    if (ci.value->kind != flat::Constant::Kind::kLiteral) {
        return;
    }

    EmitDocstring(&file_, named_const, false);
    switch (ci.type->kind) {
    case flat::Type::Kind::kPrimitive:
        GeneratePrimitiveDefine(
            named_const.name,
            static_cast<flat::PrimitiveType*>(ci.type.get())->subtype,
            static_cast<flat::LiteralConstant*>(ci.value.get())->literal->location().data());
        break;
    case flat::Type::Kind::kString:
        GenerateStringDefine(
            named_const.name,
            static_cast<flat::LiteralConstant*>(ci.value.get())->literal->location().data());
        break;
    default:
        abort();
    }
    EmitBlank(&file_);
}

void DdkGenerator::ProduceProtocolImplementation(const NamedInterface& named_interface) {
    const auto& proto_name = named_interface.snake_case_name;

    if (named_interface.type == InterfaceType::kCallback) {
        assert(named_interface.methods.size() == 1 && "callback should only have 1 function");

        file_ << "struct " << proto_name << " {\n";
        const auto& method_info = named_interface.methods[0];
        std::vector<Member> input;
        std::vector<Member> output;
        GetMethodParameters(library_, method_info, &input, &output, false);

        file_ << kIndent;
        EmitProtocolMethodPtrDecl(&file_, method_info.c_name, input, output);
        file_ << ");\n";
        file_ << kIndent << "void* ctx;\n";
        file_ << "};\n";
        EmitBlank(&file_);
        return;
    }

    file_ << "typedef struct " << proto_name << "_ops {\n";
    for (const auto& method_info : named_interface.methods) {
        std::vector<Member> input;
        std::vector<Member> output;
        GetMethodParameters(library_, method_info, &input, &output, false);

        file_ << kIndent;
        EmitProtocolMethodPtrDecl(&file_, method_info.c_name, input, output);
        file_ << ");\n";
    }
    file_ << "} " << proto_name << "_ops_t;\n";
    EmitBlank(&file_);

    // Emit Protocol.
    EmitDocstring(&file_, named_interface, false);
    file_ << "struct " << proto_name << " {\n";
    file_ << kIndent << proto_name << "_ops_t* ops;\n";
    file_ << kIndent << "void* ctx;\n";
    file_ << "};\n";
    EmitBlank(&file_);

    // Emit Protocol helper functions.
    for (const auto& method_info : named_interface.methods) {
        std::vector<Member> input;
        std::vector<Member> output;
        GetMethodParameters(library_, method_info, &input, &output, false);

        EmitDocstring(&file_, method_info, false);
        file_ << "static inline ";
        EmitProtocolMethodWithSpecificCtxDecl(
            &file_, proto_name, method_info.protocol_name, input, output);
        file_ << ") {\n"
              << kIndent;
        EmitDdkProtocolMethodImpl(&file_, method_info.c_name, input, output);
        file_ << "}\n";
    }
    EmitBlank(&file_);

    // Emit Protocol async helper functions.
    for (const auto& method_info : named_interface.methods) {
        if (!method_info.async || !method_info.generate_sync_method) continue;
        // Generate context struct.
        std::vector<DdkGenerator::Member> members;
        members.reserve(method_info.output_parameters.size() + 1);
        members.push_back(DdkGenerator::Member{
            .kind = flat::Type::Kind::kIdentifier,
            .decl_kind = flat::Decl::Kind::kStruct,
            .type = "sync_completion_t",
            .name = "completion",
            .element_type = "",
            .array_counts = {},
            .nullability = types::Nullability::kNonnullable,
        });
        for (const auto& member : method_info.output_parameters) {
            members.push_back(CreateMember(library_, member, false));
        }
        GenerateStructDeclaration(method_info.protocol_name + "_callback_context", members, false,
                                  true);
        EmitBlank(&file_);

        // Generate callback function.
        members.erase(members.begin());
        file_ << "static ";
        EmitMethodDeclHelper(&file_, method_info.protocol_name + "_cb", members, {},
                             "void* cookie");
        file_ << ") {\n";
        EmitCallbackMethodImpl(&file_, method_info.protocol_name, members);
        file_ << "}\n";
        EmitBlank(&file_);

        // Generated sync version of helper function.
        auto method_info2 = method_info;
        method_info2.async = false;
        std::vector<Member> input;
        std::vector<Member> output;
        GetMethodParameters(library_, method_info2, &input, &output, false);

        file_ << "static inline ";
        EmitProtocolMethodWithSpecificCtxDecl(
            &file_, proto_name, method_info.protocol_name + "_sync", input,
            output);
        file_ << ") {\n";

        input.clear();
        output.clear();
        GetMethodParameters(library_, method_info, &input, &output, false);
        EmitSyncMethodImpl(&file_, proto_name, method_info.protocol_name, input, members);
        file_ << "}\n";
    }
}

void DdktlGenerator::ProduceExample(const NamedInterface& named_interface) {
    if (named_interface.type == InterfaceType::kCallback ||
        named_interface.type == InterfaceType::kInterface)
        return;

    const auto& shortname = named_interface.shortname;
    const auto& sc_name = named_interface.snake_case_name;
    const auto& cc_name = named_interface.camel_case_name;
    const auto lc_name = ToLispCase(sc_name);

    file_ << "// DDK " << ToLispCase(sc_name) << " support\n";
    file_ << "//\n";
    file_ << "// :: Proxies ::\n";
    file_ << "//\n";
    file_ << "// ddk::" << cc_name << "Client is a simple wrapper around\n";
    file_ << "// " << sc_name << "_t. It does not own the pointers passed to it\n";
    file_ << "//\n";
    file_ << "// :: Mixins ::\n";
    file_ << "//\n";
    file_ << "// ddk::" << cc_name << " is a mixin class that simplifies writing DDK "
          << "drivers\n";
    file_ << "// that implement the " << ToLispCase(shortname)
          << " protocol. It doesn't set the base protocol.\n";
    file_ << "//\n";
    file_ << "// :: Examples ::\n";
    file_ << "//\n";
    file_ << "// // A driver that implements a ZX_PROTOCOL_" << ToSnakeCase(shortname, true)
          << " device.\n";
    file_ << "// class " << shortname << "Device {\n";
    file_ << "// using " << shortname << "DeviceType = ddk::Device<" << shortname
          << "Device, /* ddk mixins */>;\n";
    file_ << "//\n";
    file_ << "// class " << shortname << "Device : public " << shortname << "DeviceType,\n";
    file_ << "// " << std::string(shortname.size() + 15, ' ') << "public ddk::" << cc_name << "<"
          << shortname << "Device> {\n";
    file_ << "//   public:\n";
    file_ << "// " << kIndent << shortname << "Device(zx_device_t* parent)\n";
    file_ << "// " << kIndent << kIndent << ": " << shortname << "DeviceType(\"my-" << lc_name
          << "-device\", parent) {}\n";
    file_ << "//\n";
    for (const auto& method_info : named_interface.methods) {
        std::vector<Member> input;
        std::vector<Member> output;
        GetMethodParameters(library_, method_info, &input, &output,
                            named_interface.handle_wrappers);

        file_ << "// " << kIndent;
        EmitProtocolMethodDecl(&file_, method_info.protocol_name, input, output);
        file_ << ");\n";
        file_ << "//\n";
    }
    file_ << "// " << kIndent << "...\n";
    file_ << "// };\n";
    EmitBlank(&file_);
}

void DdktlGenerator::ProduceProtocolImplementation(const NamedInterface& named_interface) {
    if (named_interface.type == InterfaceType::kCallback) return;

    const auto& sc_name = named_interface.snake_case_name;
    const auto& cc_name = named_interface.camel_case_name;

    const auto& ops = sc_name + "_ops_";

    EmitDocstring(&file_, named_interface, false);
    file_ << "template <typename D, typename Base = internal::base_mixin>\n";
    file_ << "class " << cc_name << " : public Base {\n";
    file_ << "public:\n";
    file_ << kIndent << cc_name << "() {\n";
    file_ << kIndent << kIndent << "internal::Check" << cc_name << "Subclass<D>();\n";
    for (const auto& method_info : named_interface.methods) {
        file_ << kIndent << kIndent << ops << "." << method_info.c_name
              << " = " << method_info.protocol_name << ";\n";
    }
    if (named_interface.type != InterfaceType::kInterface) {
        EmitBlank(&file_);
        file_ << kIndent << kIndent << "if constexpr (internal::is_base_proto<Base>::value) {\n";
        file_ << kIndent << kIndent << kIndent << "auto dev = static_cast<D*>(this);\n";
        file_ << kIndent << kIndent << kIndent
              << "// Can only inherit from one base_protocol implementation.\n";
        file_ << kIndent << kIndent << kIndent << "ZX_ASSERT(dev->ddk_proto_id_ == 0);\n";
        file_ << kIndent << kIndent << kIndent << "dev->ddk_proto_id_ = ZX_PROTOCOL_"
            << ToSnakeCase(named_interface.shortname, true) << ";\n";
        file_ << kIndent << kIndent << kIndent << "dev->ddk_proto_ops_ = &" << ops << ";\n";
        file_ << kIndent << kIndent << "}\n";
    }
    file_ << kIndent << "}\n";
    EmitBlank(&file_);
    file_ << "protected:\n";
    file_ << kIndent << sc_name << "_ops_t " << ops << " = {};\n";
    EmitBlank(&file_);
    file_ << "private:\n";
    for (const auto& method_info : named_interface.methods) {
        std::vector<Member> input;
        std::vector<Member> output;
        GetMethodParameters(library_, method_info, &input, &output, false);

        EmitDocstring(&file_, method_info, true);
        file_ << kIndent << "static ";
        EmitProtocolMethodWithCtxDecl(&file_, method_info.protocol_name, input, output);
        file_ << ") {\n";
        if (named_interface.handle_wrappers) {
            std::vector<Member> input2;
            std::vector<Member> output2;
            GetMethodParameters(library_, method_info, &input2, &output2, true);
            EmitDdktlProtocolMethodImpl(&file_, method_info.protocol_name, input2, output2, true);
        } else {
            EmitDdktlProtocolMethodImpl(&file_, method_info.protocol_name, input, output, false);
        }
        file_ << kIndent << "}\n";
    }
    file_ << "};\n";
    EmitBlank(&file_);

    ProduceClientImplementation(named_interface);
}

void DdktlGenerator::ProduceClientImplementation(const NamedInterface& named_interface) {
    if (named_interface.type == InterfaceType::kCallback) return;

    const auto& sc_name = named_interface.snake_case_name;
    const auto& cc_name = named_interface.camel_case_name;

    const auto type = sc_name + "_t";
    const auto proto_id = "ZX_PROTOCOL_" + ToSnakeCase(named_interface.shortname, true);

    file_ << "class " << cc_name << "Client {\n";
    file_ << "public:\n";
    file_ << kIndent << cc_name << "Client()\n";
    file_ << kIndent << kIndent << ": ops_(nullptr), ctx_(nullptr) {}\n";
    file_ << kIndent << cc_name << "Client(const " << type << "* proto)\n";
    file_ << kIndent << kIndent << ": ops_(proto->ops), ctx_(proto->ctx) {}\n";
    if (named_interface.type != InterfaceType::kInterface) {
        EmitBlank(&file_);
        file_ << kIndent << cc_name << "Client(zx_device_t* parent) {\n";
        file_ << kIndent << kIndent << type << " proto;\n";
        file_ << kIndent << kIndent << "if (device_get_protocol(parent, " << proto_id
              << ", &proto) == ZX_OK) {\n";
        file_ << kIndent << kIndent << kIndent << "ops_ = proto.ops;\n";
        file_ << kIndent << kIndent << kIndent << "ctx_ = proto.ctx;\n";
        file_ << kIndent << kIndent << "} else {\n";
        file_ << kIndent << kIndent << kIndent << "ops_ = nullptr;\n";
        file_ << kIndent << kIndent << kIndent << "ctx_ = nullptr;\n";
        file_ << kIndent << kIndent << "}\n";
        file_ << kIndent << "}\n";
    }
    EmitBlank(&file_);
    file_ << kIndent << "void GetProto(" << type << "* proto) const {\n";
    file_ << kIndent << kIndent << "proto->ctx = ctx_;\n";
    file_ << kIndent << kIndent << "proto->ops = ops_;\n";
    file_ << kIndent << "}\n";
    file_ << kIndent << "bool is_valid() const {\n";
    file_ << kIndent << kIndent << "return ops_ != nullptr;\n";
    file_ << kIndent << "}\n";
    file_ << kIndent << "void clear() {\n";
    file_ << kIndent << kIndent << "ctx_ = nullptr;\n";
    file_ << kIndent << kIndent << "ops_ = nullptr;\n";
    file_ << kIndent << "}\n";
    for (const auto& method_info : named_interface.methods) {
        std::vector<Member> input;
        std::vector<Member> output;
        GetMethodParameters(library_, method_info, &input, &output,
                            named_interface.handle_wrappers);

        EmitDocstring(&file_, method_info, true);
        file_ << kIndent;
        EmitProtocolMethodDecl(&file_, method_info.proxy_name, input, output);
        file_ << ") const {\n"
              << kIndent << kIndent;
        EmitClientMethodImpl(&file_, method_info.c_name, input, output,
                             named_interface.handle_wrappers);
        file_ << kIndent << "}\n";
    }
    EmitBlank(&file_);
    file_ << "private:\n";
    file_ << kIndent << sc_name << "_ops_t* ops_;\n";
    file_ << kIndent << "void* ctx_;\n";
    file_ << "};\n";
    EmitBlank(&file_);
}

void DdktlGenerator::ProduceProtocolSubclass(const NamedInterface& named_interface) {
    if (named_interface.type == InterfaceType::kCallback) return;

    const auto& sc_name = named_interface.snake_case_name;
    const auto& cc_name = named_interface.camel_case_name;

    for (const auto& method_info : named_interface.methods) {
        std::vector<Member> input;
        std::vector<Member> output;
        GetMethodParameters(library_, method_info, &input, &output,
                            named_interface.handle_wrappers);

        file_ << "DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_" << sc_name << "_" << method_info.c_name
              << ", " << method_info.protocol_name << ",\n";
        file_ << kIndent << kIndent;
        EmitProtocolMethodTemplateDecl(&file_, input, output);
    }
    EmitBlank(&file_);

    file_ << "template <typename D>\n";
    file_ << "constexpr void Check" << cc_name << "Subclass() {\n";
    for (const auto& method_info : named_interface.methods) {
        std::vector<Member> input;
        std::vector<Member> output;
        GetMethodParameters(library_, method_info, &input, &output,
                            named_interface.handle_wrappers);

        file_ << kIndent << "static_assert(internal::has_" << sc_name << "_" << method_info.c_name
              << "<D>::value,\n";
        file_ << kIndent << kIndent << "\"" << cc_name << " subclasses must implement \"\n";
        file_ << kIndent << kIndent << "\"";
        EmitProtocolMethodDecl(&file_, method_info.protocol_name, input, output);
        file_ << "\");\n";
    }
    file_ << "}\n";
    EmitBlank(&file_);
}

void DdkGenerator::ProduceStructDeclaration(const NamedStruct& named_struct) {
    // TODO: Hack - structs with no members are defined in a different header.
    if (named_struct.struct_info.members.empty()) return;

    std::vector<DdkGenerator::Member> members =
        GenerateMembers(library_, named_struct.struct_info.members);
    EmitDocstring(&file_, named_struct, false);
    GenerateStructDeclaration(named_struct.name, members, named_struct.packed);

    EmitBlank(&file_);
}

void DdkGenerator::ProduceUnionDeclaration(const NamedUnion& named_union) {
    std::vector<DdkGenerator::Member> members =
        GenerateMembers(library_, named_union.union_info.members);
    EmitDocstring(&file_, named_union, false);
    GenerateTaggedUnionDeclaration(named_union.name, members);

    EmitBlank(&file_);
}

std::ostringstream DdkGenerator::ProduceHeader() {
    std::map<const flat::Decl*, NamedConst> named_consts =
        NameConsts(library_->const_declarations_);
    std::map<const flat::Decl*, NamedEnum> named_enums = NameEnums(library_->enum_declarations_);
    std::map<const flat::Decl*, NamedInterface> named_interfaces =
        NameInterfaces(library_->interface_declarations_);
    std::map<const flat::Decl*, NamedStruct> named_structs =
        NameStructs(library_->struct_declarations_);
    std::map<const flat::Decl*, NamedUnion> named_unions =
        NameUnions(library_->union_declarations_);

    GeneratePrologues();

    file_ << "\n// Forward declarations\n\n";

    for (const auto* decl : library_->declaration_order_) {
        switch (decl->kind) {
        case flat::Decl::Kind::kConst: {
            auto iter = named_consts.find(decl);
            if (iter != named_consts.end()) {
                ProduceConstForwardDeclaration(iter->second);
            }
            break;
        }
        case flat::Decl::Kind::kEnum: {
            auto iter = named_enums.find(decl);
            if (iter != named_enums.end()) {
                ProduceEnumForwardDeclaration(iter->second);
            }
            break;
        }
        case flat::Decl::Kind::kInterface: {
            auto iter = named_interfaces.find(decl);
            if (iter != named_interfaces.end()) {
                ProduceProtocolForwardDeclaration(iter->second);
            }
            break;
        }
        case flat::Decl::Kind::kStruct: {
            auto iter = named_structs.find(decl);
            if (iter != named_structs.end()) {
                ProduceStructForwardDeclaration(iter->second);
            }
            break;
        }
        case flat::Decl::Kind::kUnion: {
            auto iter = named_unions.find(decl);
            if (iter != named_unions.end()) {
                ProduceUnionForwardDeclaration(iter->second);
            }
            break;
        }
        default:
            abort();
        }
    }

    file_ << "\n// Declarations\n\n";

    for (const auto* decl : library_->declaration_order_) {
        switch (decl->kind) {
        case flat::Decl::Kind::kConst: {
            auto iter = named_consts.find(decl);
            if (iter != named_consts.end()) {
                ProduceConstDeclaration(iter->second);
            }
            break;
        }
        case flat::Decl::Kind::kEnum:
            // Enums can be entirely forward declared, as they have no
            // dependencies other than standard headers.
            break;
        case flat::Decl::Kind::kInterface: {
            auto iter = named_interfaces.find(decl);
            if (iter != named_interfaces.end()) {
                ProduceProtocolImplementation(iter->second);
            }
            break;
        }
        case flat::Decl::Kind::kStruct: {
            auto iter = named_structs.find(decl);
            if (iter != named_structs.end()) {
                ProduceStructDeclaration(iter->second);
            }
            break;
        }
        case flat::Decl::Kind::kUnion: {
            auto iter = named_unions.find(decl);
            if (iter != named_unions.end()) {
                ProduceUnionDeclaration(iter->second);
            }
            break;
        }
        default:
            abort();
        }
    }

    GenerateEpilogues();

    return std::move(file_);
}

std::ostringstream DdktlGenerator::ProduceHeader() {
    std::map<const flat::Decl*, NamedInterface> named_interfaces =
        NameInterfaces(library_->interface_declarations_);

    GeneratePrologues();
    for (const auto& iter : named_interfaces) {
        ProduceExample(iter.second);
    }

    EmitNamespacePrologue(&file_, "ddk");
    EmitBlank(&file_);

    for (const auto* decl : library_->declaration_order_) {
        switch (decl->kind) {
        case flat::Decl::Kind::kInterface: {
            auto iter = named_interfaces.find(decl);
            if (iter != named_interfaces.end()) {
                ProduceProtocolImplementation(iter->second);
            }
            break;
        }
        default:
            continue;
        }
    }

    GenerateEpilogues();

    return std::move(file_);
}

std::ostringstream DdktlGenerator::ProduceInternalHeader() {
    std::map<const flat::Decl*, NamedInterface> named_interfaces =
        NameInterfaces(library_->interface_declarations_);

    EmitFileComment(&file_, library_->name().back());
    EmitHeaderGuard(&file_);
    EmitBlank(&file_);
    EmitIncludeHeader(&file_, "<" + ToLispCase(StringJoin(library_->name(), "/")) + ".h>");
    EmitIncludeHeader(&file_, "<type_traits>");
    EmitBlank(&file_);
    EmitNamespacePrologue(&file_, "ddk");
    EmitNamespacePrologue(&file_, "internal");
    EmitBlank(&file_);

    for (const auto* decl : library_->declaration_order_) {
        switch (decl->kind) {
        case flat::Decl::Kind::kInterface: {
            auto iter = named_interfaces.find(decl);
            if (iter != named_interfaces.end()) {
                ProduceProtocolSubclass(iter->second);
            }
            break;
        }
        default:
            break;
        }
    }

    EmitNamespaceEpilogue(&file_, "internal");
    EmitNamespaceEpilogue(&file_, "ddk");

    return std::move(file_);
}
} // namespace banjo
