// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_UTEST_FIDL_COMPILER_TEST_LIBRARY_H_
#define ZIRCON_SYSTEM_UTEST_FIDL_COMPILER_TEST_LIBRARY_H_

#include <fidl/flat_ast.h>
#include <fidl/json_generator.h>
#include <fidl/lexer.h>
#include <fidl/linter.h>
#include <fidl/parser.h>
#include <fidl/source_file.h>

static std::unique_ptr<fidl::SourceFile> MakeSourceFile(const std::string& filename,
                                                        const std::string& raw_source_code) {
  std::string source_code(raw_source_code);
  // NUL terminate the string.
  source_code.resize(source_code.size() + 1);
  return std::make_unique<fidl::SourceFile>(filename, source_code);
}

class SharedAmongstLibraries {
 public:
  SharedAmongstLibraries() : typespace(fidl::flat::Typespace::RootTypes(&error_reporter)) {}

  fidl::ErrorReporter error_reporter;
  fidl::flat::Typespace typespace;
  fidl::flat::Libraries all_libraries;
  std::vector<std::unique_ptr<fidl::SourceFile>> all_sources_of_all_libraries;
};

class TestLibrary final {
 public:
  explicit TestLibrary() : TestLibrary(&owned_shared_) {}

  explicit TestLibrary(SharedAmongstLibraries* shared)
      : error_reporter_(&shared->error_reporter),
        typespace_(&shared->typespace),
        all_libraries_(&shared->all_libraries),
        all_sources_of_all_libraries_(&shared->all_sources_of_all_libraries),
        library_(
            std::make_unique<fidl::flat::Library>(all_libraries_, error_reporter_, typespace_)) {}

  explicit TestLibrary(const std::string& raw_source_code)
      : TestLibrary("example.fidl", raw_source_code) {}

  TestLibrary(const std::string& filename, const std::string& raw_source_code)
      : TestLibrary(filename, raw_source_code, &owned_shared_) {}

  TestLibrary(const std::string& filename, const std::string& raw_source_code,
              SharedAmongstLibraries* shared)
      : TestLibrary(shared) {
    AddSource(filename, raw_source_code);
  }

  void AddSource(const std::string& filename, const std::string& raw_source_code) {
    auto source_file = MakeSourceFile(filename, raw_source_code);
    all_sources_.push_back(source_file.get());
    all_sources_of_all_libraries_->push_back(std::move(source_file));
  }

  bool AddDependentLibrary(TestLibrary dependent_library) {
    return all_libraries_->Insert(std::move(dependent_library.library_));
  }

  void AddAttributeSchema(const std::string& name, fidl::flat::AttributeSchema schema) {
    all_libraries_->AddAttributeSchema(name, std::move(schema));
  }

  // TODO(pascallouis): remove, this does not use a library.
  bool Parse(std::unique_ptr<fidl::raw::File>* out_ast_ptr) {
    assert(all_sources_.size() == 1 && "parse can only be used with one source");
    auto source_file = all_sources_.at(0);
    fidl::Lexer lexer(*source_file, error_reporter_);
    fidl::Parser parser(&lexer, error_reporter_);
    out_ast_ptr->reset(parser.Parse().release());
    return parser.Ok();
  }

  bool Compile() {
    for (auto source_file : all_sources_) {
      fidl::Lexer lexer(*source_file, error_reporter_);
      fidl::Parser parser(&lexer, error_reporter_);
      auto ast = parser.Parse();
      if (!parser.Ok())
        return false;
      if (!library_->ConsumeFile(std::move(ast)))
        return false;
    }
    return library_->Compile();
  }

  // TODO(pascallouis): remove, this does not use a library.
  bool Lint(fidl::Findings* findings, const std::set<std::string>& included_check_ids = {},
            const std::set<std::string>& excluded_check_ids = {}, bool exclude_by_default = false,
            std::set<std::string>* excluded_checks_not_found = nullptr) {
    assert(all_sources_.size() == 1 && "lint can only be used with one source");
    auto source_file = all_sources_.at(0);
    fidl::Lexer lexer(*source_file, error_reporter_);
    fidl::Parser parser(&lexer, error_reporter_);
    auto ast = parser.Parse();
    if (!parser.Ok()) {
      std::string_view beginning(source_file->data().data(), 0);
      fidl::SourceSpan span(beginning, *source_file);
      findings->emplace_back(span, "parser-error", error_reporter_->errors().front() + "\n");
      return false;
    }
    fidl::linter::Linter linter;
    if (!included_check_ids.empty()) {
      linter.set_included_checks(included_check_ids);
    }
    if (!excluded_check_ids.empty()) {
      linter.set_excluded_checks(excluded_check_ids);
    }
    linter.set_exclude_by_default(exclude_by_default);
    return linter.Lint(ast, findings, excluded_checks_not_found);
  }

  bool Lint() {
    fidl::Findings findings;
    bool passed = Lint(&findings);
    fidl::utils::WriteFindingsToErrorReporter(findings, error_reporter_);
    return passed;
  }

  std::string GenerateJSON() {
    auto json_generator = fidl::JSONGenerator(library_.get());
    auto out = json_generator.Produce();
    return out.str();
  }

  const fidl::flat::Bits* LookupBits(const std::string& name) {
    for (const auto& bits_decl : library_->bits_declarations_) {
      if (bits_decl->GetName() == name) {
        return bits_decl.get();
      }
    }
    return nullptr;
  }

  const fidl::flat::Const* LookupConstant(const std::string& name) {
    for (const auto& const_decl : library_->const_declarations_) {
      if (const_decl->GetName() == name) {
        return const_decl.get();
      }
    }
    return nullptr;
  }

  const fidl::flat::Enum* LookupEnum(const std::string& name) {
    for (const auto& enum_decl : library_->enum_declarations_) {
      if (enum_decl->GetName() == name) {
        return enum_decl.get();
      }
    }
    return nullptr;
  }

  const fidl::flat::Service* LookupService(const std::string& name) {
    for (const auto& service_decl : library_->service_declarations_) {
      if (service_decl->GetName() == name) {
        return service_decl.get();
      }
    }
    return nullptr;
  }

  const fidl::flat::Struct* LookupStruct(const std::string& name) {
    for (const auto& struct_decl : library_->struct_declarations_) {
      if (struct_decl->GetName() == name) {
        return struct_decl.get();
      }
    }
    return nullptr;
  }

  const fidl::flat::Table* LookupTable(const std::string& name) {
    for (const auto& table_decl : library_->table_declarations_) {
      if (table_decl->GetName() == name) {
        return table_decl.get();
      }
    }
    return nullptr;
  }

  const fidl::flat::TypeAlias* LookupTypeAlias(const std::string& name) {
    for (const auto& type_alias_decl : library_->type_alias_declarations_) {
      if (type_alias_decl->GetName() == name) {
        return type_alias_decl.get();
      }
    }
    return nullptr;
  }

  const fidl::flat::Union* LookupUnion(const std::string& name) {
    for (const auto& union_decl : library_->union_declarations_) {
      if (union_decl->GetName() == name) {
        return union_decl.get();
      }
    }
    return nullptr;
  }

  const fidl::flat::XUnion* LookupXUnion(const std::string& name) {
    for (const auto& xunion_decl : library_->xunion_declarations_) {
      if (xunion_decl->GetName() == name) {
        return xunion_decl.get();
      }
    }
    return nullptr;
  }

  const fidl::flat::Protocol* LookupProtocol(const std::string& name) {
    for (const auto& protocol_decl : library_->protocol_declarations_) {
      if (protocol_decl->GetName() == name) {
        return protocol_decl.get();
      }
    }
    return nullptr;
  }

  void set_warnings_as_errors(bool value) { error_reporter_->set_warnings_as_errors(value); }

  const fidl::flat::Library* library() const { return library_.get(); }

  const fidl::SourceFile& source_file() const {
    assert(all_sources_.size() == 1 && "convenience method only possible with single source");
    return *all_sources_.at(0);
  }

  fidl::SourceSpan source_span(size_t start, size_t size) const {
    assert(all_sources_.size() == 1 && "convenience method only possible with single source");
    std::string_view data = all_sources_.at(0)->data();
    data.remove_prefix(start);
    data.remove_suffix(data.size() - size);
    return fidl::SourceSpan(data, *all_sources_.at(0));
  }

  const std::vector<std::string>& errors() const { return error_reporter_->errors(); }

  const std::vector<std::string>& warnings() const { return error_reporter_->warnings(); }

  const std::vector<fidl::flat::Decl*> declaration_order() const {
    return library_->declaration_order_;
  }

 protected:
  SharedAmongstLibraries owned_shared_;
  fidl::ErrorReporter* error_reporter_;
  fidl::flat::Typespace* typespace_;
  fidl::flat::Libraries* all_libraries_;
  std::vector<std::unique_ptr<fidl::SourceFile>>* all_sources_of_all_libraries_;
  std::vector<fidl::SourceFile*> all_sources_;
  std::unique_ptr<fidl::flat::Library> library_;
};

#endif  // ZIRCON_SYSTEM_UTEST_FIDL_COMPILER_TEST_LIBRARY_H_
