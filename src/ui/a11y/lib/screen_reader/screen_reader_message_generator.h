// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SCREEN_READER_SCREEN_READER_MESSAGE_GENERATOR_H_
#define SRC_UI_A11Y_LIB_SCREEN_READER_SCREEN_READER_MESSAGE_GENERATOR_H_

#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include <fuchsia/accessibility/tts/cpp/fidl.h>
#include <lib/zx/time.h>

#include <memory>
#include <unordered_map>
#include <vector>

#include "fuchsia/intl/l10n/cpp/fidl.h"
#include "src/ui/a11y/lib/screen_reader/i18n/message_formatter.h"

namespace a11y {

// The ScreenReaderMessageGenerator creates screen reader output (node descriptions, hints, etc.),
// which is spoken to the user by a tts system. For example, a semantic node which is a button,
// with label 'ok', could be represented as: Utterance: 'ok', (with 200 ms delay) Utterance:
// 'button'.
class ScreenReaderMessageGenerator {
 public:
  // Holds an utterance and some metadata used to control how it should be spoken.
  struct UtteranceAndContext {
    // The utterance to be spoken.
    fuchsia::accessibility::tts::Utterance utterance;
    // The delay that should be introduced before this utterance is spoken.
    zx::duration delay = zx::msec(0);
  };

  // Holds the row and column headers for a table cell. Each entry is ONLY set
  // if it's different from the row/column header of the previously focused
  // node.
  struct TableCellContext {
    std::string row_header;
    std::string column_header;
  };

  // Holds relevant information about a node's surrounding context that may be
  // required to describe the node.
  struct ScreenReaderMessageContext {
    // The node corresponding to the container to which the described node
    // belongs. A value of nullptr indicates that the described node does not
    // belong to a container. This field is used to give users a hint that they have
    // entered or exited a container, and describe the container if the former.
    const fuchsia::accessibility::semantics::Node* current_container = nullptr;

    // The node corresponding to the container to which the previuosly described
    // node belongs. A value of nullptr indicates that the previously described
    // node did not belong to a container. This field is used to give users a
    // hint that they have entered or exited a container.
    const fuchsia::accessibility::semantics::Node* previous_container = nullptr;

    // We need to treat the case where a user has exited a nested container
    // differently from the case where a user has exited a top-level container
    // and immediately entered a new one. These cases are indistinguishable
    // without semantic knowledge (which the ScreenReaderMessageGenerator
    // doesn't have), so we need an extra bit of state.
    bool exited_nested_container = false;

    // Holds information required to describe a table cell node that's not
    // present in the node's data.
    std::optional<TableCellContext> table_cell_context;

    ScreenReaderMessageContext()
        : current_container(nullptr),
          previous_container(nullptr),
          table_cell_context(std::nullopt) {}

    // Returns true if the current and/or previous containers are
    // describable.
    bool HasDescribableContainer() { return current_container || previous_container; }
  };

  // |message_formatter| is the resourses object used by this class tto retrieeve localized message
  // strings by their unique MessageId. The language used is the language loaded in
  // |message_formatter|.
  explicit ScreenReaderMessageGenerator(std::unique_ptr<i18n::MessageFormatter> message_formatter);
  virtual ~ScreenReaderMessageGenerator() = default;

  // Returns a description of the semantic node.
  virtual std::vector<UtteranceAndContext> DescribeNode(
      const fuchsia::accessibility::semantics::Node* node,
      ScreenReaderMessageContext message_context = ScreenReaderMessageContext());

  // Returns an utterance for a message retrieved by message ID. If the message contains positional
  // named arguments, they must be passed in |arg_names|, with corresponding values in |arg_values|.
  // Please see MessageFormatter for a full documentation on named arguments.
  virtual UtteranceAndContext GenerateUtteranceByMessageId(
      fuchsia::intl::l10n::MessageIds message_id, zx::duration delay = zx::msec(0),
      const std::vector<std::string>& arg_names = std::vector<std::string>(),
      const std::vector<std::string>& arg_values = std::vector<std::string>());

  // Returns an utterance that describes a character to be used when spelling a word or entering
  // text. For example, the symbol '.' may be described as 'dot', if the current language is
  // English. If the symbol is not known, the symbol itself is returned. Note that a string is the
  // parameter here because not all UTF-8 grapheme clusters can be represented in a char.
  virtual UtteranceAndContext FormatCharacterForSpelling(const std::string& character);

  i18n::MessageFormatter* message_formatter_for_test() { return message_formatter_.get(); }

 protected:
  // Constructor for mock only.
  ScreenReaderMessageGenerator() = default;

 private:
  // Gives the appropriate entered/exited container hint and describes the
  // current container, if necessary.
  std::vector<ScreenReaderMessageGenerator::UtteranceAndContext> DescribeContainer(
      ScreenReaderMessageContext message_context);

  // Helper method to describe a node that is a radio button.
  UtteranceAndContext DescribeRadioButton(const fuchsia::accessibility::semantics::Node* node);

  // Helper method to describe a node that is a check box. The resulting description can be one or
  // more utterances, depending on the ammount of semantic data available about the state of the
  // node (checked / not checked for example).
  std::vector<UtteranceAndContext> DescribeCheckBox(
      const fuchsia::accessibility::semantics::Node* node);

  // Helper method to describe a node that is a toggle switch.
  UtteranceAndContext DescribeToggleSwitch(const fuchsia::accessibility::semantics::Node* node);

  // Helper method to describe a node that represents a table.
  // The message will be:
  //
  // <label>, <row count> rows, <column count> columns, table
  std::vector<UtteranceAndContext> DescribeTable(
      const fuchsia::accessibility::semantics::Node* node);

  // Helper method to describe a node that represents a table cell.
  // The message will be:
  //
  // <label>, <row header>*, <column header>*, <row span>^, <column
  // span>^, <row index>, <column index>, table cell
  //
  // *If different from the previous cell read.
  // ^If greater than 1.
  std::vector<UtteranceAndContext> DescribeTableCell(
      const fuchsia::accessibility::semantics::Node* node,
      ScreenReaderMessageContext message_context);

  // Helper method to describe a node that represents a table row/column header.
  // The message will be:
  //
  // <label>, row <row span>, spans <row/column span> rows/columns, row/column
  // header
  std::vector<UtteranceAndContext> DescribeRowOrColumnHeader(
      const fuchsia::accessibility::semantics::Node* node);

  std::unique_ptr<i18n::MessageFormatter> message_formatter_;

  std::unordered_map<std::string, fuchsia::intl::l10n::MessageIds> character_to_message_id_;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_SCREEN_READER_SCREEN_READER_MESSAGE_GENERATOR_H_
