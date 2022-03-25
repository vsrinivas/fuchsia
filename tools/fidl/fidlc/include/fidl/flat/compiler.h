// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_COMPILER_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_COMPILER_H_

#include <lib/fit/function.h>

#include <memory>

#include "fidl/experimental_flags.h"
#include "fidl/flat/attribute_schema.h"
#include "fidl/flat/typespace.h"
#include "fidl/flat_ast.h"
#include "fidl/ordinals.h"
#include "fidl/virtual_source_file.h"

namespace fidl::flat {

class Libraries;

// Compiler consumes raw::File ASTs and produces a compiled flat::Library.
class Compiler final : private ReporterMixin {
 public:
  Compiler(Libraries* all_libraries, ordinals::MethodHasher method_hasher,
           ExperimentalFlags experimental_flags);
  Compiler(const Compiler&) = delete;

  bool ConsumeFile(std::unique_ptr<raw::File> file);
  // Returns the library if compilation was successful, otherwise returns null.
  std::unique_ptr<Library> Compile();

  // Step is the base class for compilation steps. Compiling a library consists
  // of performing all steps in sequence. Each step succeeds (no additional
  // errors) or fails (additional errors reported) as a unit, and typically
  // tries to process the entire library rather than stopping after the first
  // error. For certain major steps, we abort compilation if the step fails,
  // meaning later steps can rely on invariants from that step succeeding.
  class Step : protected ReporterMixin {
   public:
    explicit Step(Compiler* compiler) : ReporterMixin(compiler->reporter()), compiler_(compiler) {}
    Step(const Step&) = delete;

    bool Run();

   protected:
    Compiler* compiler() { return compiler_; }
    Library* library() { return compiler_->library_.get(); }
    const Libraries* all_libraries() { return compiler_->all_libraries_; }
    Typespace* typespace();
    VirtualSourceFile* generated_source_file();
    const ordinals::MethodHasher& method_hasher() { return compiler_->method_hasher_; }
    const ExperimentalFlags& experimental_flags() { return compiler_->experimental_flags_; }

   private:
    // Implementations must report errors via ReporterMixin. If no errors are
    // reported, the step is considered successful.
    virtual void RunImpl() = 0;

    Compiler* compiler_;
  };

 private:
  std::unique_ptr<Library> library_;
  Libraries* all_libraries_;
  ordinals::MethodHasher method_hasher_;
  const ExperimentalFlags experimental_flags_;
};

// Libraries manages a set of compiled libraries along with resources common to
// all of them (e.g. the shared typespace). The libraries must be inserted in
// order: first the dependencies, with each one only depending on those that
// came before it, and lastly the target library.
class Libraries : private ReporterMixin {
 public:
  explicit Libraries(Reporter* reporter)
      : ReporterMixin(reporter),
        root_library_(Library::CreateRootLibrary()),
        typespace_(root_library_.get(), reporter),
        attribute_schemas_(AttributeSchema::OfficialAttributes()) {}
  Libraries(const Libraries&) = delete;
  Libraries(Libraries&&) = default;

  // Insert |library|. It must only depend on already-inserted libraries.
  bool Insert(std::unique_ptr<Library> library);

  // Lookup a library by its |library_name|, or returns null if none is found.
  Library* Lookup(const std::vector<std::string_view>& library_name) const;

  // Removes a library that was inserted before.
  //
  // TODO(fxbug.dev/90838): This is only needed to filter out the zx library,
  // and should be deleted once that is no longer necessary.
  void Remove(const Library* library);

  // Returns true if no libraries have been inserted.
  bool Empty() const { return libraries_.empty(); }

  // Returns the root library, which defines builtin types.
  const Library* root_library() const { return root_library_.get(); }

  // Returns the target library. Must have inserted at least one library.
  const Library* target_library() const {
    assert(!libraries_.empty());
    return libraries_.back().get();
  }

  // Returns libraries that were inserted but never used, i.e. that do not occur
  // in the target libary's dependency tree. Must have inserted at least one.
  std::set<const Library*, LibraryComparator> Unused() const;

  // Returns decls from all libraries in a topologically sorted order, i.e.
  // later decls only depend on earlier ones.
  std::vector<const Decl*> DeclarationOrder() const;

  // Returns a set that is like `library->dependencies`, but also includes
  // indirect dependencies that come from protocol composition, i.e. what would
  // need to be imported if the composed methods were copied and pasted.
  std::set<const Library*, LibraryComparator> DirectAndComposedDependencies(
      const Library* library) const;

  // Registers a new attribute schema under the given name, and returns it.
  AttributeSchema& AddAttributeSchema(std::string name);

  // Gets the schema for an attribute. For unrecognized attributes, returns
  // AttributeSchema::kUserDefined. If warn_on_typo is true, reports a warning
  // if the attribute appears to be a typo for an official attribute.
  const AttributeSchema& RetrieveAttributeSchema(const Attribute* attribute,
                                                 bool warn_on_typo = false) const;

  using ReporterMixin::reporter;
  Typespace* typespace() { return &typespace_; }
  VirtualSourceFile* generated_source_file() { return &generated_source_file_; }

 private:
  std::unique_ptr<Library> root_library_;
  std::vector<std::unique_ptr<Library>> libraries_;
  std::map<std::vector<std::string_view>, Library*> libraries_by_name_;
  Typespace typespace_;
  AttributeSchemaMap attribute_schemas_;
  // TODO(fxbug.dev/8027): Remove this field.
  VirtualSourceFile generated_source_file_{"generated"};
};

}  // namespace fidl::flat

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_COMPILER_H_
