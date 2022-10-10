// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_COMPILER_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_COMPILER_H_

#include <lib/fit/function.h>

#include <memory>

#include "tools/fidl/fidlc/include/fidl/experimental_flags.h"
#include "tools/fidl/fidlc/include/fidl/flat/attribute_schema.h"
#include "tools/fidl/fidlc/include/fidl/flat/typespace.h"
#include "tools/fidl/fidlc/include/fidl/flat_ast.h"
#include "tools/fidl/fidlc/include/fidl/ordinals.h"
#include "tools/fidl/fidlc/include/fidl/reporter.h"
#include "tools/fidl/fidlc/include/fidl/versioning_types.h"
#include "tools/fidl/fidlc/include/fidl/virtual_source_file.h"

namespace fidl::flat {

class Libraries;

// Compiler consumes raw::File ASTs and produces a compiled flat::Library.
class Compiler final : private ReporterMixin {
 public:
  Compiler(Libraries* all_libraries, const VersionSelection* version_selection,
           ordinals::MethodHasher method_hasher, ExperimentalFlags experimental_flags);
  Compiler(const Compiler&) = delete;

  // Consumes a parsed file. Must be called once for each file in the library.
  bool ConsumeFile(std::unique_ptr<raw::File> file);
  // Compiles the library. Must be called once after consuming all files. On
  // success, inserts the new library into all_libraries and returns true.
  bool Compile();

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
    const VersionSelection* version_selection() { return compiler_->version_selection; }
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
  const VersionSelection* version_selection;
  ordinals::MethodHasher method_hasher_;
  const ExperimentalFlags experimental_flags_;
};

struct Compilation;

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

  // Returns the filtered compilation for the last-inserted library.
  //
  // TODO(fxbug.dev/67858): Add a method that doesn't take a version selection
  // and preserves everything, for the full-history IR needed by kazoo.
  std::unique_ptr<Compilation> Filter(const VersionSelection* version_selection);

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

  // Returns libraries that were inserted but never used, i.e. that do not occur
  // in the target libary's dependency tree. Must have inserted at least one.
  std::set<const Library*, LibraryComparator> Unused() const;

  // Returns the set of platforms that these libraries are versioned under.
  std::set<Platform, Platform::Compare> Platforms() const;

  // Registers a new attribute schema under the given name, and returns it.
  AttributeSchema& AddAttributeSchema(std::string name);

  // Gets the schema for an attribute. For unrecognized attributes, returns
  // AttributeSchema::kUserDefined.
  const AttributeSchema& RetrieveAttributeSchema(const Attribute* attribute) const;

  // Reports a warning if the given attribute appears to be a typo for an
  // official attribute.
  void WarnOnAttributeTypo(const Attribute* attribute) const;

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

// A compilation is the result of compiling a library and all its transitive
// dependencies. All fidlc output should be a function of the compilation
// (roughly speaking; of course everything is reachable via pointers into the
// AST, but we should avoid any further processing/traversals).
struct Compilation {
  // Like Library::Declarations, but with const pointers rather than unique_ptr.
  struct Declarations {
    std::vector<const Alias*> aliases;
    std::vector<const Bits*> bits;
    std::vector<const Builtin*> builtins;
    std::vector<const Const*> consts;
    std::vector<const Enum*> enums;
    std::vector<const NewType*> new_types;
    std::vector<const Protocol*> protocols;
    std::vector<const Resource*> resources;
    std::vector<const Service*> services;
    std::vector<const Struct*> structs;
    std::vector<const Table*> tables;
    std::vector<const Union*> unions;
  };

  // A library dependency together with its filtered declarations.
  struct Dependency {
    const Library* library;
    Declarations declarations;
  };

  // The target library name and attributes. Note, we purposely do not store a
  // Library* to avoid accidentally reaching into its unfiltered decls.
  std::vector<std::string_view> library_name;
  // Location where the target library is defined.
  std::vector<SourceSpan> library_declarations;
  // Stores all library references defined with using directives.
  std::vector<std::pair<Library*, SourceSpan>> using_references;

  const AttributeList* library_attributes;

  // Filtered from library->declarations.
  Declarations declarations;

  // Filtered from structs used as method payloads in protocols that come from
  // an external library via composition.
  std::vector<const Struct*> external_structs;

  // Filtered from library->declaration_order.
  std::vector<const Decl*> declaration_order;

  // Filtered from the combined declaration_order of the target library and all
  // its transitive dependencies, in a single topologically sorted list.
  std::vector<const Decl*> all_libraries_declaration_order;

  // Filtered from library->dependencies, and also includes indirect
  // dependencies that come from protocol composition, i.e. what would need to
  // be imported if the composed methods were copied and pasted.
  std::vector<Dependency> direct_and_composed_dependencies;
};

}  // namespace fidl::flat

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_COMPILER_H_
