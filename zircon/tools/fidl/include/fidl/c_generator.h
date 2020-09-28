// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_C_GENERATOR_H_
#define ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_C_GENERATOR_H_

#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "flat_ast.h"

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
  explicit CGenerator(const flat::Library* library) : library_(library) {}

  ~CGenerator() = default;

  std::ostringstream ProduceHeader();
  std::ostringstream ProduceClient();
  std::ostringstream ProduceServer();

  enum class Transport {
    Channel,
  };

  // TODO(fxbug.dev/56727): This should eventually be a constant described in the fidl
  // definition of the channel transport.
  static constexpr uint32_t kChannelMaxMessageHandles = 64;

  struct Member {
    flat::Type::Kind kind;
    flat::Decl::Kind decl_kind;
    std::string type;
    std::string name;
    // Name of the element type for sequential collections.
    // For (multidimensional-) arrays, it names the inner-most type.
    // For FIDL vector<T>, it names T.
    std::string element_type;
    std::vector<uint32_t> array_counts;
    types::Nullability nullability;
    // Bound on the element count for string and vector collection types.
    // When there is no limit, its value is UINT32_MAX.
    // Method parameters are pre-validated against this bound at the beginning of a FIDL call.
    uint32_t max_num_elements;
  };

  struct NamedMessage {
    std::string c_name;
    std::string coded_name;
    const std::vector<flat::Struct::Member>& parameters;
    const TypeShape typeshape;
  };

  struct NamedMethod {
    uint64_t ordinal;
    std::string ordinal_name;
    std::string identifier;
    std::string c_name;
    std::unique_ptr<NamedMessage> request;
    std::unique_ptr<NamedMessage> response;
  };

 private:
  struct NamedBits {
    std::string name;
    const flat::Bits& bits_info;
  };

  struct NamedConst {
    std::string name;
    const flat::Const& const_info;
  };

  struct NamedEnum {
    std::string name;
    const flat::Enum& enum_info;
  };

  struct NamedProtocol {
    std::string c_name;
    std::string discoverable_name;
    Transport transport;
    std::vector<NamedMethod> methods;
  };

  struct NamedStruct {
    std::string c_name;
    std::string coded_name;
    const flat::Struct& struct_info;
  };

  enum class StructKind {
    kMessage,
    kNonmessage,
  };

  uint32_t GetMaxHandlesFor(Transport transport, const TypeShape& typeshape);

  void GeneratePrologues();
  void GenerateEpilogues();

  void GenerateIntegerDefine(std::string_view name, types::PrimitiveSubtype subtype,
                             std::string_view value);
  void GenerateIntegerTypedef(types::PrimitiveSubtype subtype, std::string_view name);
  void GeneratePrimitiveDefine(std::string_view name, types::PrimitiveSubtype subtype,
                               std::string_view value);
  void GenerateStringDefine(std::string_view name, std::string_view value);
  void GenerateStructTypedef(std::string_view name);

  void GenerateStructDeclaration(std::string_view name, const std::vector<Member>& members,
                                 StructKind kind);
  void GenerateTableDeclaration(std::string_view name);
  void GenerateTaggedUnionDeclaration(std::string_view name, const std::vector<Member>& members);

  std::map<const flat::Decl*, NamedBits> NameBits(
      const std::vector<std::unique_ptr<flat::Bits>>& bits_infos);
  std::map<const flat::Decl*, NamedConst> NameConsts(
      const std::vector<std::unique_ptr<flat::Const>>& const_infos);
  std::map<const flat::Decl*, NamedEnum> NameEnums(
      const std::vector<std::unique_ptr<flat::Enum>>& enum_infos);
  std::map<const flat::Decl*, NamedProtocol> NameProtocols(
      const std::vector<std::unique_ptr<flat::Protocol>>& protocol_infos);
  std::map<const flat::Decl*, NamedStruct> NameStructs(
      const std::vector<std::unique_ptr<flat::Struct>>& struct_infos);

  void ProduceBitsForwardDeclaration(const NamedBits& named_bits);
  void ProduceConstForwardDeclaration(const NamedConst& named_const);
  void ProduceEnumForwardDeclaration(const NamedEnum& named_enum);
  void ProduceProtocolForwardDeclaration(const NamedProtocol& named_protocol);
  void ProduceStructForwardDeclaration(const NamedStruct& named_struct);

  void ProduceProtocolExternDeclaration(const NamedProtocol& named_protocol);

  void ProduceConstDeclaration(const NamedConst& named_const);
  void ProduceMessageDeclaration(const NamedMessage& named_message);
  void ProduceProtocolDeclaration(const NamedProtocol& named_protocol);
  void ProduceStructDeclaration(const NamedStruct& named_struct);

  void ProduceProtocolClientDeclaration(const NamedProtocol& named_protocol);
  void ProduceProtocolClientImplementation(const NamedProtocol& named_protocol);

  void ProduceProtocolServerDeclaration(const NamedProtocol& named_protocol);
  void ProduceProtocolServerImplementation(const NamedProtocol& named_protocol);

  const flat::Library* library_;
  std::ostringstream file_;
};

}  // namespace fidl

#endif  // ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_C_GENERATOR_H_
