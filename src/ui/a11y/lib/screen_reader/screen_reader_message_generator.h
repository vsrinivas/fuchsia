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
    // All containers from which we just exited, sorted 'deepest-last'.
    std::vector<const fuchsia::accessibility::semantics::Node*> exited_containers;

    // All containers which we just entered, sorted 'deepest-first'.
    std::vector<const fuchsia::accessibility::semantics::Node*> entered_containers;

    // If the focused node's nearest ancestor container is a table, this holds extra
    // information required to describe it. Only holds information that has changed since the
    // previous message.
    std::optional<TableCellContext> changed_table_cell_context;
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
      const std::vector<i18n::MessageFormatter::ArgValue>& arg_values =
          std::vector<i18n::MessageFormatter::ArgValue>());

  // Returns an utterance that spells out a single character.
  // 1. Pronounces single symbols: For example, the label '.' may be described as 'dot', if the
  // current language is English.
  // 2. Pronounces capital letters: For example, the label 'A' may be described as 'capital A'.
  // If neither of these occur, the original label will be returned.
  //
  // Note that 'character' should only be a single character (its type is 'string' because not all
  // UTF-8 grapheme clusters can be represented in a 'char').
  //
  // Virtual for testing.
  virtual UtteranceAndContext DescribeCharacterForSpelling(const std::string& character);

  // Helper method that describes a list element marker.
  // If the node label only contains a single symbol, instead pronounces a canonicalized version
  // (e.g., " •" becomes "bullet").
  UtteranceAndContext DescribeListElementMarkerLabel(const std::string& label);

  i18n::MessageFormatter* message_formatter_for_test() { return message_formatter_.get(); }

 protected:
  // Constructor for mock only.
  ScreenReaderMessageGenerator() = default;

 private:
  // Gives any appropriate entered/exited container hints. Also describes the
  // current container, if it's changed.
  void DescribeContainerChanges(const ScreenReaderMessageContext& message_context,
                                std::vector<UtteranceAndContext>& description);

  // Describe a node that doesn't require any special-case logic.
  void DescribeTypicalNode(const fuchsia::accessibility::semantics::Node* node,
                           std::vector<UtteranceAndContext>& description);

  // Helper method to describe button nodes. The description will be:
  // <selected*> <label> button <toggled*> <actionable>
  //
  // * = if applicable
  void DescribeButton(const fuchsia::accessibility::semantics::Node* node,
                      std::vector<UtteranceAndContext>& description);

  // Helper method to describe a node that is a radio button.
  void DescribeRadioButton(const fuchsia::accessibility::semantics::Node* node,
                           std::vector<UtteranceAndContext>& description);

  // Helper method to describe a node that is a check box. The resulting description can be one or
  // more utterances, depending on the ammount of semantic data available about the state of the
  // node (checked / not checked for example).
  void DescribeCheckBox(const fuchsia::accessibility::semantics::Node* node,
                        std::vector<UtteranceAndContext>& description);

  // Helper method to describe a node that is a list element marker. The only
  // difference is that we may canonicalize special-character labels
  // (motivating example: labels like '•' should be pronunced 'bullet').
  void DescribeListElementMarker(const fuchsia::accessibility::semantics::Node* node,
                                 std::vector<UtteranceAndContext>& description);

  // Helper method to describe a node that is a toggle switch.
  void DescribeToggleSwitch(const fuchsia::accessibility::semantics::Node* node,
                            std::vector<UtteranceAndContext>& description);

  // Helper method to describe a node that is a slider.
  void DescribeSlider(const fuchsia::accessibility::semantics::Node* node,
                      std::vector<UtteranceAndContext>& description);

  // Helper method to describe a node that represents a table.
  // The message will be:
  //
  // <label>, <row count> rows, <column count> columns, table
  void DescribeTable(const fuchsia::accessibility::semantics::Node* node,
                     std::vector<UtteranceAndContext>& description);

  // Helper method to describe a node that represents a table cell.
  // The message will be:
  //
  // <label>, <row header>*, <column header>*, <row span>^, <column
  // span>^, <row index>, <column index>, table cell
  //
  // *If different from the previous cell read.
  // ^If greater than 1.
  void DescribeTableCell(const fuchsia::accessibility::semantics::Node* node,
                         ScreenReaderMessageContext message_context,
                         std::vector<UtteranceAndContext>& description);

  // Helper method to describe a node that represents a table row/column header.
  // The message will be:
  //
  // <label>, row <row span>, spans <row/column span> rows/columns, row/column
  // header
  void DescribeRowOrColumnHeader(const fuchsia::accessibility::semantics::Node* node,
                                 std::vector<UtteranceAndContext>& description);

  // Helper method to describe entering a node that represents a list.
  // The message will be:
  //
  // Entered list with [0 items / 1 item / n items], <label>.
  // *If present.
  void DescribeEnteredList(const fuchsia::accessibility::semantics::Node* node,
                           std::vector<UtteranceAndContext>& description);

  // Add the node's label to its description, if present.
  void MaybeAddLabelDescriptor(const fuchsia::accessibility::semantics::Node* node,
                               std::vector<UtteranceAndContext>& description);

  // Add the node's role to its description, if present.
  void MaybeAddRoleDescriptor(const fuchsia::accessibility::semantics::Node* node,
                              std::vector<UtteranceAndContext>& description);

  // Describe the node as "selected", if the node is selected.
  //
  // We do NOT describe unselected buttons as "unselected", because some
  // runtimes may set the "selected" state to false by default, as opposed to
  //
  // Only use this method for "generically selectable" node types: UNKNOWN and
  // BUTTON.
  void MaybeAddGenericSelectedDescriptor(
      const fuchsia::accessibility::semantics::Node* node,
      std::vector<ScreenReaderMessageGenerator::UtteranceAndContext>* description);

  // Add the "double-tap to activate" hint to |description| if |node| has a
  // DEFAULT action.
  void MaybeAddDoubleTapHint(
      const fuchsia::accessibility::semantics::Node* node,
      std::vector<ScreenReaderMessageGenerator::UtteranceAndContext>& description);

  std::unique_ptr<i18n::MessageFormatter> message_formatter_;

  std::unordered_map<std::string, fuchsia::intl::l10n::MessageIds> character_to_message_id_;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_SCREEN_READER_SCREEN_READER_MESSAGE_GENERATOR_H_
