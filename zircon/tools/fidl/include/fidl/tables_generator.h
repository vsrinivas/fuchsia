// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_TABLES_GENERATOR_H_
#define ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_TABLES_GENERATOR_H_

#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "coded_ast.h"
#include "coded_types_generator.h"
#include "flat_ast.h"

namespace fidl {

// |TablesGenerator| generates metadata about FIDL types useful for encoding or
// decoding. The definition of these metadata structures is located in
// //zircon/system/ulib/fidl/include/lib/fidl/internal.h
//
// Methods or functions named "Emit..." are the actual interface to
// the tables output.
//
// Methods named "Generate..." directly generate tables output via the
// "Emit" routines.
//
// Methods named "Produce..." indirectly generate tables output by calling
// the Generate methods, and should not call the "Emit" functions
// directly.
//
// Note that this file is specifically emitted as a C file rather than a C++
// file in order to ensure that the definitions of the tables will be in .rodata
// or .data, not runtime initialized. This is necessary because there are
// consumers of the data that run before main(), and the order of initialization
// by ctor is not guaranteed (see fxbug.dev/39978).
class TablesGenerator {
 public:
  explicit TablesGenerator(const flat::Library* library) : library_(library) {}

  ~TablesGenerator() = default;

  std::ostringstream Produce();

 private:
  template <typename Collection>
  void GenerateArray(const Collection& collection);

  void Generate(const coded::EnumType& struct_type);
  void Generate(const coded::BitsType& struct_type);
  void Generate(const coded::StructType& struct_type);
  void Generate(const coded::TableType& table_type);
  void Generate(const coded::XUnionType& xunion_type);
  void Generate(const coded::StructPointerType& pointer);
  void Generate(const coded::MessageType& message_type);
  void Generate(const coded::HandleType& handle_type);
  void Generate(const coded::ProtocolHandleType& protocol_type);
  void Generate(const coded::RequestHandleType& request_type);
  void Generate(const coded::ArrayType& array_type);
  void Generate(const coded::StringType& string_type);
  void Generate(const coded::VectorType& vector_type);

  enum class CastToFidlType {
    kNoCast = 0,
    kCast = 1,
  };
  void Generate(const coded::Type* type, CastToFidlType cast_to_fidl_type = CastToFidlType::kCast);
  void Generate(const coded::StructField& field);
  void Generate(const coded::StructPadding& padding);
  void Generate(const coded::StructElement& element);
  void Generate(const coded::TableField& field);
  void Generate(const coded::XUnionField& field);

  void GenerateForward(const coded::EnumType& enum_type);
  void GenerateForward(const coded::BitsType& bits_type);
  void GenerateForward(const coded::StructType& struct_type);
  void GenerateForward(const coded::TableType& table_type);
  void GenerateForward(const coded::XUnionType& xunion_type);

  void GenerateTypedef(const coded::EnumType& enum_type);
  void GenerateTypedef(const coded::BitsType& bits_type);
  void GenerateTypedef(const coded::StructType& struct_type);
  void GenerateTypedef(const coded::TableType& table_type);
  void GenerateTypedef(const coded::XUnionType& xunion_type);

  void Produce(CodedTypesGenerator* coded_types_generator);

  const flat::Library* library_;

  // These will be empty after calling Produce(), since Produce() std::moves
  // them into the result.
  std::ostringstream tables_file_;
  std::ostringstream forward_decls_;

  size_t indent_level_ = 0u;
};

}  // namespace fidl

#endif  // ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_TABLES_GENERATOR_H_
