// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_CALLBACK_TREE_VISITOR_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_CALLBACK_TREE_VISITOR_H_

#include <vector>

#include <fidl/tree_visitor.h>
#include <lib/fit/function.h>

namespace fidl {
namespace linter {

// Supports TreeVisitor actions via delegation instead of inheritance, by
// wrapping a TreeVisitor subclass that calls a list of callbacks for
// each visitor method. In otherwords, this class implements a hardcoded "map"
// from each source node type (represented by its TreeVisitor method) to a set
// of callbacks, rather than implementing the callback logic directly inside
// the overridden method.
class LintingTreeCallbacks {
public:
    // Construct a new callbacks container. Call "On" methods, for each event
    // type (such as "OnAttribute"), to register a callback for that event.
    LintingTreeCallbacks();

    // Process a file (initiates the callbacks as each element is visited for
    // the given parsed source file).
    void Visit(std::unique_ptr<raw::File> const& element);

    // Register a callback for a "File" event. All of the remaining "On"
    // functions similarly match their cooresponding TreeVisitor methods.
    void OnFile(fit::function<void(const raw::File&)> callback) {
        file_callbacks_.push_back(std::move(callback));
    }
    void OnUsing(fit::function<void(const raw::Using&)> callback) {
        using_callbacks_.push_back(std::move(callback));
    }
    void OnConstDeclaration(fit::function<void(const raw::ConstDeclaration&)> callback) {
        const_declaration_callbacks_.push_back(std::move(callback));
    }
    void OnEnumMember(fit::function<void(const raw::EnumMember&)> callback) {
        enum_member_callbacks_.push_back(std::move(callback));
    }
    void OnInterfaceDeclaration(fit::function<void(const raw::InterfaceDeclaration&)> callback) {
        interface_declaration_callbacks_.push_back(std::move(callback));
    }
    void OnStructMember(fit::function<void(const raw::StructMember&)> callback) {
        struct_member_callbacks_.push_back(std::move(callback));
    }
    void OnTableMember(fit::function<void(const raw::TableMember&)> callback) {
        table_member_callbacks_.push_back(std::move(callback));
    }
    void OnUnionMember(fit::function<void(const raw::UnionMember&)> callback) {
        union_member_callbacks_.push_back(std::move(callback));
    }
    void OnXUnionMember(fit::function<void(const raw::XUnionMember&)> callback) {
        xunion_member_callbacks_.push_back(std::move(callback));
    }

private:
    // tree_visitor_ is initialized to a locally-defined class
    // |CallbackTreeVisitor| defined in the out-of-line implementation of the
    // LintingTreeCallbacks constructor.
    //
    // The CallbackTreeVisitor overrides TreeVisitor to call the registered
    // callback methods. It is not necessary to define the class inline here.
    // We avoid having to declare all of the overridden methods unnecessarily
    // in this header; and avoid the alternative--defining the methods inline,
    // in this header--which would make inclusing the header a costly thing to
    // do.
    std::unique_ptr<fidl::raw::TreeVisitor> tree_visitor_;

    std::vector<fit::function<void(const raw::File&)>> file_callbacks_;
    std::vector<fit::function<void(const raw::Using&)>> using_callbacks_;
    std::vector<fit::function<void(const raw::ConstDeclaration&)>> const_declaration_callbacks_;
    std::vector<fit::function<void(const raw::EnumMember&)>> enum_member_callbacks_;
    std::vector<fit::function<void(const raw::InterfaceDeclaration&)>> interface_declaration_callbacks_;
    std::vector<fit::function<void(const raw::StructMember&)>> struct_member_callbacks_;
    std::vector<fit::function<void(const raw::TableMember&)>> table_member_callbacks_;
    std::vector<fit::function<void(const raw::UnionMember&)>> union_member_callbacks_;
    std::vector<fit::function<void(const raw::XUnionMember&)>> xunion_member_callbacks_;
};

} // namespace linter
} // namespace fidl

#endif // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_CALLBACK_TREE_VISITOR_H_
