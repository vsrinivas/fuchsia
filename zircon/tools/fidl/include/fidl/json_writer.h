// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_JSON_WRITER_H_
#define ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_JSON_WRITER_H_

#include <lib/fit/function.h>

#include <ostream>
#include <string_view>
#include <vector>

namespace fidl {
namespace utils {

// Methods or functions named "Emit..." are the actual protocol to
// the JSON output.

// Other public methods take various value types and generate JSON
// output via the "Emit" routines.

// |JsonWriter| requires the derived type as a template parameter so it can
// match methods declared with parameter overrides in the derived class.
template <typename DerivedT>
class JsonWriter {
 public:
  JsonWriter(std::ostream& os, int indent_level = 0) : os_(os), indent_level_(indent_level) {}

  ~JsonWriter() = default;

  template <typename Iterator>
  void GenerateArray(Iterator begin, Iterator end) {
    EmitArrayBegin();

    if (begin != end) {
      Indent();
      EmitNewlineWithIndent();
    }

    for (Iterator it = begin; it != end; ++it) {
      if (it != begin)
        EmitArraySeparator();
      self.Generate(*it);
    }

    if (begin != end) {
      Outdent();
      EmitNewlineWithIndent();
    }

    EmitArrayEnd();
  }

  template <typename Collection>
  void GenerateArray(const Collection& collection) {
    self.GenerateArray(collection.begin(), collection.end());
  }

  template <typename T>
  void Generate(const std::unique_ptr<T>& value) {
    self.Generate(*value);
  }

  template <typename T>
  void Generate(const std::vector<T>& value) {
    self.GenerateArray(value);
  }

  void Generate(bool value) { EmitBoolean(value); }

  void Generate(std::string_view value) { EmitString(value); }

  void Generate(std::string value) { EmitString(value); }

  void Generate(uint32_t value) { EmitNumeric(value); }
  void Generate(int64_t value) { EmitNumeric(value); }
  void Generate(uint64_t value) { EmitNumeric(value); }

  void ResetIndentLevel() { indent_level_ = 0; }

  void Indent() { indent_level_++; }

  void Outdent() { indent_level_--; }

  // Similar to |this| pointer, the |self| reference is simply pre-cast to the
  // derived type. Methods called with template-typed parameters (such as
  // this->Generate(obj)) would only see the Generate() methods and acceptable
  // parameter types defined within this base class, but when called using
  // self.Generate(obj), the type of obj can be matched to all Generate()
  // methods declared by both the derived type and exposed from its base
  // class (this class).
  DerivedT& self = *static_cast<DerivedT*>(this);

 protected:
  enum class Position {
    kFirst,
    kSubsequent,
  };

  // ConstantStyle indicates whether the constant value to be emitted should be
  // directly placed in the JSON output, or whether is must be wrapped in a
  // string.
  enum ConstantStyle {
    kAsConstant,
    kAsString,
  };

  void GenerateEOF() { EmitNewline(); }

  void GenerateObjectPunctuation(Position position) {
    switch (position) {
      case Position::kFirst:
        Indent();
        EmitNewlineWithIndent();
        break;
      case Position::kSubsequent:
        EmitObjectSeparator();
        break;
    }
  }

  void GenerateObject(fit::closure callback) {
    int original_indent_level = indent_level_;

    EmitObjectBegin();

    callback();

    if (indent_level_ > original_indent_level) {
      Outdent();
      EmitNewlineWithIndent();
    }

    EmitObjectEnd();
  }

  template <typename Type>
  void GenerateObjectMember(std::string_view key, const Type& value,
                            Position position = Position::kSubsequent) {
    GenerateObjectPunctuation(position);
    EmitObjectKey(key);
    self.Generate(value);
  }

  void EmitBoolean(bool value, ConstantStyle style = kAsConstant) {
    if (style == kAsString)
      os_ << "\"";
    if (value)
      os_ << "true";
    else
      os_ << "false";
    if (style == kAsString)
      os_ << "\"";
  }

  void EmitString(std::string_view value) {
    os_ << "\"";

    for (size_t i = 0; i < value.size(); ++i) {
      const char c = value[i];
      switch (c) {
        case '"':
          os_ << "\\\"";
          break;
        case '\\':
          os_ << "\\\\";
          break;
        case '\n':
          os_ << "\\n";
          break;
        // TODO(FIDL-28): Escape more characters.
        default:
          os_ << c;
          break;
      }
    }
    os_ << "\"";
  }

  void EmitLiteral(std::string_view value) { os_.rdbuf()->sputn(value.data(), value.size()); }

  template <typename ValueType>
  void EmitNumeric(ValueType value, ConstantStyle style = kAsConstant) {
    static_assert(std::is_arithmetic<ValueType>::value && !std::is_same<ValueType, bool>::value,
                  "EmitNumeric can only be used with a numeric ValueType!");
    static_assert(std::is_arithmetic<ValueType>::value && !std::is_same<ValueType, uint8_t>::value,
                  "EmitNumeric does not work for uint8_t, upcast to uint64_t");
    static_assert(std::is_arithmetic<ValueType>::value && !std::is_same<ValueType, int8_t>::value,
                  "EmitNumeric does not work for int8_t, upcast to int64_t");

    switch (style) {
      case ConstantStyle::kAsConstant:
        os_ << value;
        break;
      case ConstantStyle::kAsString:
        os_ << "\"" << value << "\"";
        break;
    }
  }

  void EmitNewline() { os_ << "\n"; }

  void EmitNewlineWithIndent() {
    os_ << "\n";
    int indent_level = indent_level_;
    while (indent_level--)
      os_ << kIndent;
  }

  void EmitObjectBegin() { os_ << "{"; }

  void EmitObjectSeparator() {
    os_ << ",";
    EmitNewlineWithIndent();
  }

  void EmitObjectEnd() { os_ << "}"; }

  void EmitObjectKey(std::string_view key) {
    EmitString(key);
    os_ << ": ";
  }

  void EmitArrayBegin() { os_ << "["; }

  void EmitArraySeparator() {
    os_ << ",";
    EmitNewlineWithIndent();
  }

  void EmitArrayEnd() { os_ << "]"; }

 private:
  static constexpr const char* kIndent = "  ";
  std::ostream& os_;
  int indent_level_;
};

}  // namespace utils
}  // namespace fidl

#endif  // ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_JSON_WRITER_H_
