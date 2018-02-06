// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_C_GENERATOR_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_C_GENERATOR_H_

#include <sstream>
#include <string>
#include <vector>

#include "coded_ast.h"
#include "library.h"
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
    explicit CGenerator(Library* library) : library_(library) {}

    ~CGenerator() = default;

    void ProduceCStructs(std::ostringstream* header_file_out);

    enum struct IntegerConstantType {
        kStatus,
        kInt8,
        kInt16,
        kInt32,
        kInt64,
        kUint8,
        kUint16,
        kUint32,
        kUint64,
    };

    struct Member {
        std::string type;
        std::string name;
        std::vector<uint32_t> array_counts;
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

    struct NamedMessage {
        std::string c_name;
        std::string coded_name;
        const std::vector<flat::Interface::Method::Parameter>& parameters;
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

    void GenerateIntegerDefine(StringView name, IntegerConstantType type, StringView value);
    void GenerateIntegerTypedef(IntegerConstantType type, StringView name);
    void GenerateStructTypedef(StringView name);

    void GenerateStructDeclaration(StringView name, const std::vector<Member>& members);
    void GenerateTaggedUnionDeclaration(StringView name, const std::vector<Member>& members);

    void MaybeProduceCodingField(std::string field_name, uint32_t offset, const ast::Type* type,
                                 std::vector<coded::Field>* fields);

    std::vector<NamedConst> NameConsts(const std::vector<flat::Const>& const_infos);
    std::vector<NamedEnum> NameEnums(const std::vector<flat::Enum>& enum_infos);
    std::vector<NamedMessage> NameInterfaces(const std::vector<flat::Interface>& interface_infos);
    std::vector<NamedStruct> NameStructs(const std::vector<flat::Struct>& struct_infos);
    std::vector<NamedUnion> NameUnions(const std::vector<flat::Union>& union_infos);

    void ProduceConstForwardDeclaration(const NamedConst& named_const);
    void ProduceEnumForwardDeclaration(const NamedEnum& named_enum);
    void ProduceMessageForwardDeclaration(const NamedMessage& named_message);
    void ProduceStructForwardDeclaration(const NamedStruct& named_struct);
    void ProduceUnionForwardDeclaration(const NamedUnion& named_union);

    void ProduceMessageExternDeclaration(const NamedMessage& named_message);

    void ProduceConstDeclaration(const NamedConst& named_const);
    void ProduceMessageDeclaration(const NamedMessage& named_message);
    void ProduceStructDeclaration(const NamedStruct& named_struct);
    void ProduceUnionDeclaration(const NamedUnion& named_union);

    Library* library_;
    std::ostringstream header_file_;
};

} // namespace fidl

#endif // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_C_GENERATOR_H_
