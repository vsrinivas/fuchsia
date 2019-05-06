// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/linting_tree_callbacks.h>

namespace fidl {
namespace linter {

LintingTreeCallbacks::LintingTreeCallbacks() {

    // Anonymous derived class; unique to the LintingTreeCallbacks
    class CallbackTreeVisitor : public fidl::raw::DeclarationOrderTreeVisitor {
    private:
        const LintingTreeCallbacks& callbacks_;

    public:
        CallbackTreeVisitor(const LintingTreeCallbacks& callbacks)
            : callbacks_(callbacks) {}

        void OnFile(std::unique_ptr<raw::File> const& element) override {
            for (auto& callback : callbacks_.file_callbacks_) {
                callback(*element.get());
            }
            DeclarationOrderTreeVisitor::OnFile(element);
        }
        void OnUsing(std::unique_ptr<raw::Using> const& element) override {
            for (auto& callback : callbacks_.using_callbacks_) {
                callback(*element.get());
            }
            DeclarationOrderTreeVisitor::OnUsing(element);
        }
        void OnConstDeclaration(std::unique_ptr<raw::ConstDeclaration> const& element) override {
            for (auto& callback : callbacks_.const_declaration_callbacks_) {
                callback(*element.get());
            }
            DeclarationOrderTreeVisitor::OnConstDeclaration(element);
        }
        void OnBitsDeclaration(std::unique_ptr<raw::BitsDeclaration> const& element) override {
            for (auto& callback : callbacks_.bits_declaration_callbacks_) {
                callback(*element.get());
            }
            DeclarationOrderTreeVisitor::OnBitsDeclaration(element);
        }
        void OnEnumMember(std::unique_ptr<raw::EnumMember> const& element) override {
            for (auto& callback : callbacks_.enum_member_callbacks_) {
                callback(*element.get());
            }
            DeclarationOrderTreeVisitor::OnEnumMember(element);
        }
        void OnEnumDeclaration(std::unique_ptr<raw::EnumDeclaration> const& element) override {
            for (auto& callback : callbacks_.enum_declaration_callbacks_) {
                callback(*element.get());
            }
            DeclarationOrderTreeVisitor::OnEnumDeclaration(element);
        }
        void OnInterfaceDeclaration(std::unique_ptr<raw::InterfaceDeclaration> const& element) override {
            for (auto& callback : callbacks_.interface_declaration_callbacks_) {
                callback(*element.get());
            }
            DeclarationOrderTreeVisitor::OnInterfaceDeclaration(element);
        }
        void OnStructMember(std::unique_ptr<raw::StructMember> const& element) override {
            for (auto& callback : callbacks_.struct_member_callbacks_) {
                callback(*element.get());
            }
            DeclarationOrderTreeVisitor::OnStructMember(element);
        }
        void OnStructDeclaration(std::unique_ptr<raw::StructDeclaration> const& element) override {
            for (auto& callback : callbacks_.struct_declaration_callbacks_) {
                callback(*element.get());
            }
            DeclarationOrderTreeVisitor::OnStructDeclaration(element);
        }
        void OnTableMember(std::unique_ptr<raw::TableMember> const& element) override {
            for (auto& callback : callbacks_.table_member_callbacks_) {
                callback(*element.get());
            }
            DeclarationOrderTreeVisitor::OnTableMember(element);
        }
        void OnTableDeclaration(std::unique_ptr<raw::TableDeclaration> const& element) override {
            for (auto& callback : callbacks_.table_declaration_callbacks_) {
                callback(*element.get());
            }
            DeclarationOrderTreeVisitor::OnTableDeclaration(element);
        }
        void OnUnionMember(std::unique_ptr<raw::UnionMember> const& element) override {
            for (auto& callback : callbacks_.union_member_callbacks_) {
                callback(*element.get());
            }
            DeclarationOrderTreeVisitor::OnUnionMember(element);
        }
        void OnUnionDeclaration(std::unique_ptr<raw::UnionDeclaration> const& element) override {
            for (auto& callback : callbacks_.union_declaration_callbacks_) {
                callback(*element.get());
            }
            DeclarationOrderTreeVisitor::OnUnionDeclaration(element);
        }
        void OnXUnionMember(std::unique_ptr<raw::XUnionMember> const& element) override {
            for (auto& callback : callbacks_.xunion_member_callbacks_) {
                callback(*element.get());
            }
            DeclarationOrderTreeVisitor::OnXUnionMember(element);
        }
        void OnXUnionDeclaration(std::unique_ptr<raw::XUnionDeclaration> const& element) override {
            for (auto& callback : callbacks_.xunion_declaration_callbacks_) {
                callback(*element.get());
            }
            DeclarationOrderTreeVisitor::OnXUnionDeclaration(element);
        }
    };

    tree_visitor_ = std::make_unique<CallbackTreeVisitor>(*this);
}

void LintingTreeCallbacks::Visit(std::unique_ptr<raw::File> const& element) {
    tree_visitor_->OnFile(element);
}

} // namespace linter
} // namespace fidl
