// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fstream>
#include <regex>

#include <fidl/linting_tree_callbacks.h>
#include <fidl/utils.h>

namespace fidl::linter {

LintingTreeCallbacks::LintingTreeCallbacks() {
  // Anonymous derived class; unique to the LintingTreeCallbacks
  class CallbackTreeVisitor : public fidl::raw::DeclarationOrderTreeVisitor {
   private:
    int gap_subre_count = 1;  // index 0 unused, since match starts with 1
    // Regex OR expression should try line comment first:
    const int kLineComment = gap_subre_count++;
    const int kIgnoredToken = gap_subre_count++;
    const int kWhiteSpace = gap_subre_count++;

   public:
    explicit CallbackTreeVisitor(const LintingTreeCallbacks& callbacks)
        : callbacks_(callbacks), end_of_last_gap_(nullptr), end_of_last_token_(nullptr) {
      InitGapTextRegex();
    }

    void OnFile(std::unique_ptr<raw::File> const& element) override {
      for (auto& callback : callbacks_.file_callbacks_) {
        callback(*element);
      }
      DeclarationOrderTreeVisitor::OnFile(element);
      for (auto& callback : callbacks_.exit_file_callbacks_) {
        callback(*element);
      }
    }
    void OnSourceElementStart(const raw::SourceElement& element) override {
      ProcessGapText(element.start_);
      for (auto& callback : callbacks_.source_element_callbacks_) {
        callback(element);
      }
    }
    void OnSourceElementEnd(const raw::SourceElement& element) override {
      ProcessGapText(element.end_);
    }
    void OnAliasDeclaration(std::unique_ptr<raw::AliasDeclaration> const& element) override {
      ProcessGapText(element->start_);
      for (auto& callback : callbacks_.alias_callbacks_) {
        callback(*element);
      }
      DeclarationOrderTreeVisitor::OnAliasDeclaration(element);
      ProcessGapText(element->end_);
    }
    void OnUsing(std::unique_ptr<raw::Using> const& element) override {
      ProcessGapText(element->start_);
      for (auto& callback : callbacks_.using_callbacks_) {
        callback(*element);
      }
      DeclarationOrderTreeVisitor::OnUsing(element);
      ProcessGapText(element->end_);
    }
    void OnConstDeclaration(std::unique_ptr<raw::ConstDeclaration> const& element) override {
      ProcessGapText(element->start_);
      for (auto& callback : callbacks_.const_declaration_callbacks_) {
        callback(*element);
      }
      DeclarationOrderTreeVisitor::OnConstDeclaration(element);
      for (auto& callback : callbacks_.exit_const_declaration_callbacks_) {
        callback(*element);
      }
      ProcessGapText(element->end_);
    }
    void OnProtocolDeclaration(std::unique_ptr<raw::ProtocolDeclaration> const& element) override {
      ProcessGapText(element->start_);
      for (auto& callback : callbacks_.protocol_declaration_callbacks_) {
        callback(*element);
      }
      DeclarationOrderTreeVisitor::OnProtocolDeclaration(element);
      for (auto& callback : callbacks_.exit_protocol_declaration_callbacks_) {
        callback(*element);
      }
      ProcessGapText(element->end_);
    }
    void OnProtocolMethod(std::unique_ptr<raw::ProtocolMethod> const& element) override {
      ProcessGapText(element->start_);
      if (raw::IsParameterListDefined(element->maybe_request)) {
        for (auto& callback : callbacks_.method_callbacks_) {
          callback(*element);
        }
      } else {
        for (auto& callback : callbacks_.event_callbacks_) {
          callback(*element);
        }
      }
      DeclarationOrderTreeVisitor::OnProtocolMethod(element);
      ProcessGapText(element->end_);
    }
    void OnParameter(std::unique_ptr<raw::Parameter> const& element) override {
      ProcessGapText(element->start_);
      for (auto& callback : callbacks_.parameter_callbacks_) {
        callback(*element);
      }
      DeclarationOrderTreeVisitor::OnParameter(element);
      ProcessGapText(element->end_);
    }

    // TODO(fxbug.dev/70247): Delete this.
    // --- start old syntax ---
    void OnAttributeOld(const raw::AttributeOld& element) override {
      for (auto& callback : callbacks_.attribute_old_callbacks_) {
        callback(element);
      }
    }
    void OnBitsDeclaration(std::unique_ptr<raw::BitsDeclaration> const& element) override {
      ProcessGapText(element->start_);
      for (auto& callback : callbacks_.bits_declaration_callbacks_) {
        callback(*element);
      }
      DeclarationOrderTreeVisitor::OnBitsDeclaration(element);
      for (auto& callback : callbacks_.exit_bits_declaration_callbacks_) {
        callback(*element);
      }
      ProcessGapText(element->end_);
    }
    void OnBitsMember(std::unique_ptr<raw::BitsMember> const& element) override {
      ProcessGapText(element->start_);
      for (auto& callback : callbacks_.bits_member_callbacks_) {
        callback(*element);
      }
      DeclarationOrderTreeVisitor::OnBitsMember(element);
      ProcessGapText(element->end_);
    }
    void OnEnumMember(std::unique_ptr<raw::EnumMember> const& element) override {
      ProcessGapText(element->start_);
      for (auto& callback : callbacks_.enum_member_callbacks_) {
        callback(*element);
      }
      DeclarationOrderTreeVisitor::OnEnumMember(element);
      ProcessGapText(element->end_);
    }
    void OnEnumDeclaration(std::unique_ptr<raw::EnumDeclaration> const& element) override {
      ProcessGapText(element->start_);
      for (auto& callback : callbacks_.enum_declaration_callbacks_) {
        callback(*element);
      }
      DeclarationOrderTreeVisitor::OnEnumDeclaration(element);
      for (auto& callback : callbacks_.exit_enum_declaration_callbacks_) {
        callback(*element);
      }
      ProcessGapText(element->end_);
    }
    void OnStructMember(std::unique_ptr<raw::StructMember> const& element) override {
      ProcessGapText(element->start_);
      for (auto& callback : callbacks_.struct_member_callbacks_) {
        callback(*element);
      }
      DeclarationOrderTreeVisitor::OnStructMember(element);
      ProcessGapText(element->end_);
    }
    void OnStructDeclaration(std::unique_ptr<raw::StructDeclaration> const& element) override {
      ProcessGapText(element->start_);
      for (auto& callback : callbacks_.struct_declaration_callbacks_) {
        callback(*element);
      }
      DeclarationOrderTreeVisitor::OnStructDeclaration(element);
      for (auto& callback : callbacks_.exit_struct_declaration_callbacks_) {
        callback(*element);
      }
      ProcessGapText(element->end_);
    }
    void OnTableMember(std::unique_ptr<raw::TableMember> const& element) override {
      ProcessGapText(element->start_);
      for (auto& callback : callbacks_.table_member_callbacks_) {
        callback(*element);
      }
      DeclarationOrderTreeVisitor::OnTableMember(element);
      ProcessGapText(element->end_);
    }
    void OnTableDeclaration(std::unique_ptr<raw::TableDeclaration> const& element) override {
      ProcessGapText(element->start_);
      for (auto& callback : callbacks_.table_declaration_callbacks_) {
        callback(*element);
      }
      DeclarationOrderTreeVisitor::OnTableDeclaration(element);
      for (auto& callback : callbacks_.exit_table_declaration_callbacks_) {
        callback(*element);
      }
      ProcessGapText(element->end_);
    }
    void OnTypeConstructorOld(std::unique_ptr<raw::TypeConstructorOld> const& element) override {
      for (auto& callback : callbacks_.type_constructor_old_callbacks_) {
        callback(*element);
      }
      DeclarationOrderTreeVisitor::OnTypeConstructorOld(element);
    }
    void OnUnionMember(std::unique_ptr<raw::UnionMember> const& element) override {
      ProcessGapText(element->start_);
      for (auto& callback : callbacks_.union_member_callbacks_) {
        callback(*element);
      }
      DeclarationOrderTreeVisitor::OnUnionMember(element);
      ProcessGapText(element->end_);
    }
    void OnUnionDeclaration(std::unique_ptr<raw::UnionDeclaration> const& element) override {
      ProcessGapText(element->start_);
      for (auto& callback : callbacks_.union_declaration_callbacks_) {
        callback(*element);
      }
      DeclarationOrderTreeVisitor::OnUnionDeclaration(element);
      for (auto& callback : callbacks_.exit_union_declaration_callbacks_) {
        callback(*element);
      }
      ProcessGapText(element->end_);
    }
    // --- end old syntax ---

    // --- start new syntax ---
    void OnAttributeNew(std::unique_ptr<raw::AttributeNew> const& element) override {
      for (auto& callback : callbacks_.attribute_callbacks_) {
        callback(*element);
      }
    }
    void OnOrdinaledLayoutMember(
        std::unique_ptr<raw::OrdinaledLayoutMember> const& element) override {
      ProcessGapText(element->start_);
      for (auto& callback : callbacks_.ordinaled_layout_member_callbacks_) {
        callback(*element);
      }
      DeclarationOrderTreeVisitor::OnOrdinaledLayoutMember(element);
      ProcessGapText(element->end_);
    }
    void OnStructLayoutMember(std::unique_ptr<raw::StructLayoutMember> const& element) override {
      ProcessGapText(element->start_);
      for (auto& callback : callbacks_.struct_layout_member_callbacks_) {
        callback(*element);
      }
      DeclarationOrderTreeVisitor::OnStructLayoutMember(element);
      ProcessGapText(element->end_);
    }
    void OnValueLayoutMember(std::unique_ptr<raw::ValueLayoutMember> const& element) override {
      ProcessGapText(element->start_);
      for (auto& callback : callbacks_.value_layout_member_callbacks_) {
        callback(*element);
      }
      DeclarationOrderTreeVisitor::OnValueLayoutMember(element);
      ProcessGapText(element->end_);
    }
    void OnLayout(std::unique_ptr<raw::Layout> const& element) override {
      ProcessGapText(element->start_);
      for (auto& callback : callbacks_.layout_callbacks_) {
        callback(*element);
      }
      DeclarationOrderTreeVisitor::OnLayout(element);
      for (auto& callback : callbacks_.exit_layout_callbacks_) {
        callback(*element);
      }
      ProcessGapText(element->end_);
    }
    void OnTypeDecl(std::unique_ptr<raw::TypeDecl> const& element) override {
      ProcessGapText(element->start_);
      for (auto& callback : callbacks_.type_decl_callbacks_) {
        callback(*element);
      }
      DeclarationOrderTreeVisitor::OnTypeDecl(element);
      for (auto& callback : callbacks_.exit_type_decl_callbacks_) {
        callback(*element);
      }
      ProcessGapText(element->end_);
    }
    void OnIdentifierLayoutParameter(
        std::unique_ptr<raw::IdentifierLayoutParameter> const& element) override {
      // For the time being, the the first type parameter in a layout must either be a
      // TypeConstructor (like `vector<uint8>`), or else a reference to on (like `vector<Foo>`).
      // Because of this, we can treat an IdentifierLayoutParameter as a TypeConstructor for the
      // purposes of linting.
      for (auto& callback : callbacks_.identifier_layout_parameter_callbacks_) {
        callback(*element);
      }
      DeclarationOrderTreeVisitor::OnIdentifierLayoutParameter(element);
    }
    void OnTypeConstructorNew(std::unique_ptr<raw::TypeConstructorNew> const& element) override {
      for (auto& callback : callbacks_.type_constructor_callbacks_) {
        callback(*element);
      }
      DeclarationOrderTreeVisitor::OnTypeConstructorNew(element);
    }
    // --- end new syntax ---

   private:
    void InitGapTextRegex() {
      std::string subre[gap_subre_count];

      // Regex OR expression should try line comment first:
      subre[kLineComment] = R"REGEX(//(?:\S*[ \t]*\S+)*)REGEX";
      subre[kIgnoredToken] = R"REGEX(\S+)REGEX";
      subre[kWhiteSpace] = R"REGEX((?:[ \t]+\n?)|\n)REGEX";
      // white space spanning multiple lines will be split on the
      // newline, with the newline included.

      auto regex_str = "^(" + subre[kLineComment] + ")|(" + subre[kIgnoredToken] + ")|(" +
                       subre[kWhiteSpace] + ")";

      kGapTextRegex_ = std::regex(regex_str);
    }

    // "GapText" includes everything between source elements (or between a
    // source element and the beginning or the end of the file). This
    // includes whitespace, comments, and certain tokens not processed as
    // source elements, including colons, curly braces, square brackets,
    // parentheses, commas, and semicolons.
    //
    // Break up the gap text into the different types to pass to the
    // appropriate callbacks. Include the leading characters on the line
    // as an additional parameter, to give callbacks some insight into
    // their line context. For example, is the whitespace preceded by
    // a line comment (meaning it is not a blank line)? Is the line comment
    // preceded by any non-whitespace characters (it is not a comment-
    // only line)?
    void OnGapText(std::string_view gap_view, const SourceFile& source_file,
                   std::string_view line_so_far_view) {
      auto remaining_gap_view = gap_view;
      auto remaining_line_so_far_view = line_so_far_view;
      std::string content = std::string(remaining_gap_view);
      std::smatch match;
      while (!content.empty()) {
        // The regex_search loop should consume the entire gap
        std::regex_search(content, match, kGapTextRegex_);
        assert(!match.empty() &&
               "gap content did not match any of the expected regular expressions");

        auto view = remaining_gap_view;
        view.remove_suffix(remaining_gap_view.size() - match[0].length());
        auto line_prefix_view = remaining_line_so_far_view;
        line_prefix_view.remove_suffix(remaining_gap_view.size());

        if (match[kLineComment].matched) {
          // TODO(fxbug.dev/7979): Remove FirstLineIsRegularComment() check
          // when no longer needed.
          // If there are multiple contiguous lines starting with the
          // doc comment marker ("///"), they will be merged (including
          // newlines, but with the 3 slashes stripped off) into a single
          // string, and generate an "Attribute" with |name| "Doc", and
          // |value| the multi-line string. BUT the span()
          // std::string_view for the Attribute SourceElement has ONLY the
          // first line. So when LintingTreeCallbacks processes the |gap_text|,
          // the first line is not part of the gap, but the remaining lines
          // show up as gap text comments.
          if (utils::FirstLineIsRegularComment(match[0].str())) {  // not Doc Comment
            auto line_comment = SourceSpan(view, source_file);
            for (auto& callback : callbacks_.line_comment_callbacks_) {
              // starts with the comment marker (2 slashes) and ends with
              // the last non-space character before the first newline
              callback(line_comment, line_prefix_view);
            }
          }
        } else if (match[kIgnoredToken].matched) {
          auto ignored_token = SourceSpan(view, source_file);
          for (auto& callback : callbacks_.ignored_token_callbacks_) {
            // includes (but may not be limited to): "as" : ; , { } [ ] ( )
            callback(ignored_token);
          }
        } else if (match[kWhiteSpace].matched) {
          auto white_space = SourceSpan(view, source_file);
          for (auto& callback : callbacks_.white_space_up_to_newline_callbacks_) {
            // All whitespace only (space, tab, newline)
            callback(white_space, line_prefix_view);
          }
        } else {
          assert(false && "Should never be reached. Bad regex?");
        }
        if (view.back() == '\n') {
          remaining_line_so_far_view.remove_prefix(
              (view.data() - remaining_line_so_far_view.data()) + view.size());
        }
        remaining_gap_view.remove_prefix(match[0].length());
        content = std::string(remaining_gap_view);
      }
    }

    void ProcessGapText(const fidl::Token& next_token) {
      const char* gap_start = next_token.previous_end().data().data();
      if (gap_start > end_of_last_gap_) {
        auto const& source_file = next_token.span().source_file();
        auto const& source_view = source_file.data();
        const char* source_start = source_view.data();
        const char* source_end = source_view.data() + source_view.size();

        if (gap_start > end_of_last_token_) {
          if (end_of_last_token_ == nullptr) {
            gap_start = source_start;
          } else {
            gap_start = end_of_last_token_;
          }
        }

        // The gap starts from the end of the last processed token and
        // ends with the beginning of the next token to be processed.
        // It is then broken down into either contiguous whitespace,
        // a line comment (excluding trailing whitespace at the end of
        // the line), or other characters (also called "ignored tokens,"
        // which can include the word "as", and various punctuation.)
        auto next_view = next_token.data();
        const char* gap_end = next_view.data();
        auto gap_view = source_view;
        gap_view.remove_prefix(gap_start - source_start);
        gap_view.remove_suffix(source_end - gap_end);

        // Get a view of the gap PLUS characters prior to the gap up to the
        // beginning of the line. There may still be multiple lines in the
        // gap, but the first line in the gap will be a complete line.
        // (The last line in the gap is typically not the complete line,
        // unless the next token happens to start on column 1 of the next line.
        auto line_so_far_view = source_view;
        line_so_far_view.remove_suffix(source_end - gap_end);
        if (gap_start > source_start) {
          auto preceding_newline =
              line_so_far_view.find_last_of('\n', gap_start - source_start - 1);
          if (preceding_newline != std::string_view::npos) {
            line_so_far_view.remove_prefix(preceding_newline + 1);
          }
        }

        OnGapText(gap_view, source_file, line_so_far_view);
        end_of_last_gap_ = gap_end;
        end_of_last_token_ = gap_end + next_view.size();
      }
    }

    const LintingTreeCallbacks& callbacks_;
    std::regex kGapTextRegex_;
    const char* end_of_last_gap_;
    const char* end_of_last_token_;
  };

  tree_visitor_ = std::make_unique<CallbackTreeVisitor>(*this);
}  // namespace linter

void LintingTreeCallbacks::Visit(std::unique_ptr<raw::File> const& element) {
  tree_visitor_->OnFile(element);
}

}  // namespace fidl::linter
