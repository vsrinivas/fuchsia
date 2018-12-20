// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_banjo_INCLUDE_banjo_DDK_GENERATOR_H_
#define ZIRCON_SYSTEM_HOST_banjo_INCLUDE_banjo_DDK_GENERATOR_H_

#include <sstream>
#include <string>
#include <vector>

#include "flat_ast.h"
#include "string_view.h"

namespace banjo {

// Methods or functions named "Emit..." are the actual interface to
// the C/C++ output.

// Methods named "Generate..." directly generate C/C++ output, to either
// the header or source file, via the "Emit" routines.

// Methods named "Produce..." indirectly generate C/C++ output by calling
// the Generate methods, and should not call the "Emit" functions
// directly.

class DdkGenerator {
public:
    explicit DdkGenerator(const flat::Library* library)
        : library_(library) {}

    ~DdkGenerator() = default;

    virtual std::ostringstream ProduceHeader();

    struct Member {
        flat::Type::Kind kind;
        flat::Decl::Kind decl_kind;
        std::string type;
        std::string name;
        std::string element_type;
        std::string doc;
        std::vector<uint32_t> array_counts;
        types::Nullability nullability;
        bool address_of = false;
    };

    struct NamedMethod {
        bool async;
        bool generate_sync_method;
        std::string c_name;
        std::string protocol_name;
        std::string proxy_name;
        std::string doc;
        const std::vector<flat::Interface::Method::Parameter>& input_parameters;
        const std::vector<flat::Interface::Method::Parameter>& output_parameters;
    };

protected:
    struct NamedConst {
        std::string name;
        std::string doc;
        const flat::Const& const_info;
    };

    struct NamedEnum {
        std::string name;
        std::string type_name;
        std::string doc;
        const flat::Enum& enum_info;
    };

    enum class InterfaceType {
        kProtocol,
        kDefaultProtocol,
        // Like a protocol, but not.
        kInterface,
        // One time callback.
        kCallback,
    };

    struct NamedInterface {
        InterfaceType type;
        std::string shortname;
        std::string snake_case_name;
        std::string camel_case_name;
        std::string doc;
        std::vector<NamedMethod> methods;
        // True if we wish to generate handle wrappers.
        bool handle_wrappers;
    };

    struct NamedStruct {
        std::string name;
        std::string type_name;
        std::string doc;
        bool packed;
        const flat::Struct& struct_info;
    };

    struct NamedUnion {
        std::string name;
        std::string type_name;
        std::string doc;
        const flat::Union& union_info;
    };

    virtual void GenerateEpilogues();
    virtual void GeneratePrologues();

    void GenerateIntegerDefine(StringView name, types::PrimitiveSubtype subtype, StringView value);
    virtual void GeneratePrimitiveDefine(StringView name, types::PrimitiveSubtype subtype,
                                         StringView value);
    virtual void GenerateStringDefine(StringView name, StringView value);
    void GenerateIntegerTypedef(types::PrimitiveSubtype subtype, StringView name);
    void GenerateStructTypedef(StringView name, StringView type_name);
    void GenerateUnionTypedef(StringView name, StringView type_name);

    void GenerateStructDeclaration(StringView name, const std::vector<Member>& members,
                                   bool packed, bool output = false);
    void GenerateTaggedUnionDeclaration(StringView name, const std::vector<Member>& members);

    virtual std::map<const flat::Decl*, NamedConst>
    NameConsts(const std::vector<std::unique_ptr<flat::Const>>& const_infos);
    virtual std::map<const flat::Decl*, NamedEnum>
    NameEnums(const std::vector<std::unique_ptr<flat::Enum>>& enum_infos);
    virtual std::map<const flat::Decl*, NamedInterface>
    NameInterfaces(const std::vector<std::unique_ptr<flat::Interface>>& interface_infos);
    virtual std::map<const flat::Decl*, NamedStruct>
    NameStructs(const std::vector<std::unique_ptr<flat::Struct>>& struct_infos);
    virtual std::map<const flat::Decl*, NamedUnion>
    NameUnions(const std::vector<std::unique_ptr<flat::Union>>& union_infos);

    void ProduceConstForwardDeclaration(const NamedConst& named_const);
    virtual void ProduceProtocolForwardDeclaration(const NamedInterface& named_interface);
    virtual void ProduceEnumForwardDeclaration(const NamedEnum& named_enum);
    void ProduceStructForwardDeclaration(const NamedStruct& named_struct);
    void ProduceUnionForwardDeclaration(const NamedUnion& named_union);

    void ProduceConstDeclaration(const NamedConst& named_const);
    virtual void ProduceProtocolImplementation(const NamedInterface& named_interface);
    void ProduceStructDeclaration(const NamedStruct& named_struct);
    void ProduceUnionDeclaration(const NamedUnion& named_union);

    const flat::Library* library_;
    std::ostringstream file_;
};

class DdktlGenerator : public DdkGenerator {
public:
    explicit DdktlGenerator(const flat::Library* library)
        : DdkGenerator(library) {}

    ~DdktlGenerator() = default;

    virtual std::ostringstream ProduceHeader() override;
    std::ostringstream ProduceInternalHeader();

protected:
    virtual void GeneratePrologues() override;
    virtual void GenerateEpilogues() override;

    // TODO(surajmalhotra): Define idiomatic types for C++ instead of relying on
    // C types.
#if 0
    virtual void GeneratePrimitiveDefine(StringView name, types::PrimitiveSubtype subtype,
                                         StringView value) override;
    virtual void GenerateStringDefine(StringView name, StringView value) override;

    virtual std::map<const flat::Decl*, NamedConst>
    NameConsts(const std::vector<std::unique_ptr<flat::Const>>& const_infos) override;
    virtual std::map<const flat::Decl*, NamedEnum>
    NameEnums(const std::vector<std::unique_ptr<flat::Enum>>& enum_infos) override;
#endif
    virtual std::map<const flat::Decl*, NamedInterface>
    NameInterfaces(const std::vector<std::unique_ptr<flat::Interface>>& interface_infos) override;
#if 0
    virtual std::map<const flat::Decl*, NamedStruct>
    NameStructs(const std::vector<std::unique_ptr<flat::Struct>>& struct_infos) override;
    virtual std::map<const flat::Decl*, NamedUnion>
    NameUnions(const std::vector<std::unique_ptr<flat::Union>>& union_infos) override;
#endif

#if 0
    virtual void ProduceProtocolForwardDeclaration(const NamedInterface& named_interface) override;
    virtual void ProduceEnumForwardDeclaration(const NamedEnum& named_enum) override;
#endif

    void ProduceExample(const NamedInterface& named_interface);
    virtual void ProduceProtocolImplementation(const NamedInterface& named_interface) override;
    void ProduceClientImplementation(const NamedInterface& named_interface);
    void ProduceProtocolSubclass(const NamedInterface& named_interface);
};

} // namespace banjo

#endif // ZIRCON_SYSTEM_HOST_banjo_INCLUDE_banjo_DDK_GENERATOR_H_
