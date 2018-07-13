// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_C_GENERATOR_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_C_GENERATOR_H_

#include <sstream>
#include <string>
#include <vector>

#include "flat_ast.h"
#include "string_view.h"

namespace fidl {

// Methods or functions named "Emit..." are the actual interface to
// the C output.

// Methods named "Generate..." directly generate C output, to either
// the header or source file, via the "Emit" routines.

// Methods named "Produce..." indirectly generate C output by calling
// the Generate methods, and should not call the "Emit" functions
// directly.

class CGenerator {
public:
    explicit CGenerator(const flat::Library* library)
        : library_(library) {}

    ~CGenerator() = default;

    std::ostringstream ProduceHeader();
    std::ostringstream ProduceClient();
    std::ostringstream ProduceServer();

    struct Member {
        flat::Type::Kind kind;
        flat::Decl::Kind decl_kind;
        std::string type;
        std::string name;
        std::string element_type;
        std::vector<uint32_t> array_counts;
    };

    struct NamedMessage {
        std::string c_name;
        std::string coded_name;
        const std::vector<flat::Interface::Method::Parameter>& parameters;
    };

    struct NamedMethod {
        uint32_t ordinal;
        std::string ordinal_name;
        std::string identifier;
        std::string c_name;
        std::unique_ptr<NamedMessage> request;
        std::unique_ptr<NamedMessage> response;
    };

private:
    struct NamedConst {
        std::string name;
        const flat::Const& const_info;
    };

    struct NamedEnum {
        std::string name;
        const flat::Enum& enum_info;
    };

    struct NamedInterface {
        std::string c_name;
        std::string discoverable_name;
        std::vector<NamedMethod> methods;
    };

    struct NamedStruct {
        std::string c_name;
        std::string coded_name;
        const flat::Struct& struct_info;
    };

    struct NamedUnion {
        std::string name;
        const flat::Union& union_info;
    };

    void GeneratePrologues();
    void GenerateEpilogues();

    void GenerateIntegerDefine(StringView name, types::PrimitiveSubtype subtype, StringView value);
    void GenerateIntegerTypedef(types::PrimitiveSubtype subtype, StringView name);
    void GenerateStructTypedef(StringView name);

    void GenerateStructDeclaration(StringView name, const std::vector<Member>& members);
    void GenerateTaggedUnionDeclaration(StringView name, const std::vector<Member>& members);

    std::map<const flat::Decl*, NamedConst>
    NameConsts(const std::vector<std::unique_ptr<flat::Const>>& const_infos);
    std::map<const flat::Decl*, NamedEnum>
    NameEnums(const std::vector<std::unique_ptr<flat::Enum>>& enum_infos);
    std::map<const flat::Decl*, NamedInterface>
    NameInterfaces(const std::vector<std::unique_ptr<flat::Interface>>& interface_infos);
    std::map<const flat::Decl*, NamedStruct>
    NameStructs(const std::vector<std::unique_ptr<flat::Struct>>& struct_infos);
    std::map<const flat::Decl*, NamedUnion>
    NameUnions(const std::vector<std::unique_ptr<flat::Union>>& union_infos);

    void ProduceConstForwardDeclaration(const NamedConst& named_const);
    void ProduceEnumForwardDeclaration(const NamedEnum& named_enum);
    void ProduceInterfaceForwardDeclaration(const NamedInterface& named_interface);
    void ProduceStructForwardDeclaration(const NamedStruct& named_struct);
    void ProduceUnionForwardDeclaration(const NamedUnion& named_union);

    void ProduceInterfaceExternDeclaration(const NamedInterface& named_interface);

    void ProduceConstDeclaration(const NamedConst& named_const);
    void ProduceMessageDeclaration(const NamedMessage& named_message);
    void ProduceInterfaceDeclaration(const NamedInterface& named_interface);
    void ProduceStructDeclaration(const NamedStruct& named_struct);
    void ProduceUnionDeclaration(const NamedUnion& named_union);

    void ProduceInterfaceClientDeclaration(const NamedInterface& named_interface);
    void ProduceInterfaceClientImplementation(const NamedInterface& named_interface);

    void ProduceInterfaceServerDeclaration(const NamedInterface& named_interface);
    void ProduceInterfaceServerImplementation(const NamedInterface& named_interface);

    const flat::Library* library_;
    std::ostringstream file_;
};

} // namespace fidl

#endif // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_C_GENERATOR_H_
