// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>

#include <optional>

#include "fuchsia/accessibility/semantics/cpp/fidl.h"
// This header file has been generated from the strings library fuchsia.intl.l10n.
#include "fuchsia/intl/l10n/cpp/fidl.h"
#include "src/lib/fxl/strings/trim.h"
#include "src/ui/a11y/lib/screen_reader/screen_reader_message_generator.h"
#include "src/ui/a11y/lib/screen_reader/util/util.h"

namespace a11y {
namespace {

using fuchsia::accessibility::semantics::Node;
using fuchsia::accessibility::semantics::Role;
using fuchsia::accessibility::tts::Utterance;
using fuchsia::intl::l10n::MessageIds;

static constexpr zx::duration kDefaultDelay = zx::msec(40);
static constexpr zx::duration kLongDelay = zx::msec(100);

// Currently, this just checks if the node has a DEFAULT action.
//
// todo(fxbug.dev/106566): implement better handling for secondary actions.
bool NodeIsClickable(const Node* node) {
  if (!node->has_actions()) {
    return false;
  }
  for (const auto& action : node->actions()) {
    if (action == fuchsia::accessibility::semantics::Action::DEFAULT) {
      return true;
    }
  }
  return false;
}

std::optional<MessageIds> RoleToMessageId(Role role) {
  switch (role) {
    case Role::HEADER:
      return MessageIds::ROLE_HEADER;
    case Role::IMAGE:
      return MessageIds::ROLE_IMAGE;
    case Role::LINK:
      return MessageIds::ROLE_LINK;
    case Role::TEXT_FIELD:
      return MessageIds::ROLE_TEXT_FIELD;
    case Role::SEARCH_BOX:
      return MessageIds::ROLE_SEARCH_BOX;
    case Role::SLIDER:
      return MessageIds::ROLE_SLIDER;
    default:
      return std::nullopt;
  }
}

}  // namespace

ScreenReaderMessageGenerator::ScreenReaderMessageGenerator(
    std::unique_ptr<i18n::MessageFormatter> message_formatter)
    : message_formatter_(std::move(message_formatter)) {
  character_to_message_id_.insert({"!", MessageIds::EXCLAMATION_SYMBOL_NAME});
  character_to_message_id_.insert({"?", MessageIds::QUESTION_MARK_SYMBOL_NAME});
  character_to_message_id_.insert({"_", MessageIds::UNDERSCORE_SYMBOL_NAME});
  character_to_message_id_.insert({"/", MessageIds::FORWARD_SLASH_SYMBOL_NAME});
  character_to_message_id_.insert({",", MessageIds::COMMA_SYMBOL_NAME});
  character_to_message_id_.insert({".", MessageIds::PERIOD_SYMBOL_NAME});
  character_to_message_id_.insert({"<", MessageIds::LESS_THAN_SYMBOL_NAME});
  character_to_message_id_.insert({">", MessageIds::GREATER_THAN_SYMBOL_NAME});
  character_to_message_id_.insert({"@", MessageIds::AT_SYMBOL_NAME});
  character_to_message_id_.insert({"#", MessageIds::POUND_SYMBOL_NAME});
  character_to_message_id_.insert({"$", MessageIds::DOLLAR_SYMBOL_NAME});
  character_to_message_id_.insert({"%", MessageIds::PERCENT_SYMBOL_NAME});
  character_to_message_id_.insert({"&", MessageIds::AMPERSAND_SYMBOL_NAME});
  character_to_message_id_.insert({"-", MessageIds::DASH_SYMBOL_NAME});
  character_to_message_id_.insert({"+", MessageIds::PLUS_SYMBOL_NAME});
  character_to_message_id_.insert({"=", MessageIds::EQUALS_SYMBOL_NAME});
  character_to_message_id_.insert({"(", MessageIds::LEFT_PARENTHESIS_SYMBOL_NAME});
  character_to_message_id_.insert({")", MessageIds::RIGHT_PARENTHESIS_SYMBOL_NAME});
  character_to_message_id_.insert({"\\", MessageIds::BACKSLASH_SYMBOL_NAME});
  character_to_message_id_.insert({"*", MessageIds::ASTERISK_SYMBOL_NAME});
  character_to_message_id_.insert({"\"", MessageIds::DOUBLE_QUOTATION_MARK_SYMBOL_NAME});
  character_to_message_id_.insert({"'", MessageIds::SINGLE_QUOTATION_MARK_SYMBOL_NAME});
  character_to_message_id_.insert({":", MessageIds::COLON_SYMBOL_NAME});
  character_to_message_id_.insert({";", MessageIds::SEMICOLON_SYMBOL_NAME});
  character_to_message_id_.insert({"~", MessageIds::TILDE_SYMBOL_NAME});
  character_to_message_id_.insert({"`", MessageIds::GRAVE_ACCENT_SYMBOL_NAME});
  character_to_message_id_.insert({"|", MessageIds::VERTICAL_LINE_SYMBOL_NAME});
  character_to_message_id_.insert({"√", MessageIds::SQUARE_ROOT_SYMBOL_NAME});
  character_to_message_id_.insert({"•", MessageIds::BULLET_SYMBOL_NAME});
  character_to_message_id_.insert({"◦", MessageIds::WHITE_BULLET_SYMBOL_NAME});
  character_to_message_id_.insert({"▪", MessageIds::BLACK_SQUARE_SYMBOL_NAME});
  character_to_message_id_.insert({"‣", MessageIds::TRIANGULAR_BULLET_SYMBOL_NAME});
  character_to_message_id_.insert({"⁃", MessageIds::HYPHEN_BULLET_SYMBOL_NAME});
  character_to_message_id_.insert({"✕", MessageIds::MULTIPLICATION_SYMBOL_NAME});
  character_to_message_id_.insert({"÷", MessageIds::DIVISION_SYMBOL_NAME});
  character_to_message_id_.insert({"¶", MessageIds::PILCROW_SYMBOL_NAME});
  character_to_message_id_.insert({"π", MessageIds::PI_SYMBOL_NAME});
  character_to_message_id_.insert({"∆", MessageIds::DELTA_SYMBOL_NAME});
  character_to_message_id_.insert({"£", MessageIds::BRITISH_POUND_SYMBOL_NAME});
  character_to_message_id_.insert({"¢", MessageIds::CENT_SYMBOL_NAME});
  character_to_message_id_.insert({"€", MessageIds::EURO_SYMBOL_NAME});
  character_to_message_id_.insert({"¥", MessageIds::YEN_SYMBOL_NAME});
  character_to_message_id_.insert({"^", MessageIds::CARET_SYMBOL_NAME});
  character_to_message_id_.insert({"°", MessageIds::DEGREE_SYMBOL_NAME});
  character_to_message_id_.insert({"{", MessageIds::LEFT_CURLY_BRACKET_SYMBOL_NAME});
  character_to_message_id_.insert({"}", MessageIds::RIGHT_CURLY_BRACKET_SYMBOL_NAME});
  character_to_message_id_.insert({"©", MessageIds::COPYRIGHT_SYMBOL_NAME});
  character_to_message_id_.insert({"®", MessageIds::REGISTERED_TRADEMARK_SYMBOL_NAME});
  character_to_message_id_.insert({"™", MessageIds::TRADEMARK_SYMBOL_NAME});
  character_to_message_id_.insert({"[", MessageIds::LEFT_SQUARE_BRACKET_SYMBOL_NAME});
  character_to_message_id_.insert({"]", MessageIds::RIGHT_SQUARE_BRACKET_SYMBOL_NAME});
  character_to_message_id_.insert({"¡", MessageIds::INVERTED_EXCLAMATION_POINT_SYMBOL_NAME});
  character_to_message_id_.insert({"¿", MessageIds::INVERTED_QUESTION_MARK_SYMBOL_NAME});
}

void ScreenReaderMessageGenerator::DescribeContainerChanges(
    const ScreenReaderMessageContext& message_context,
    std::vector<UtteranceAndContext>& description) {
  // Give hints for exited containers.
  for (auto container : message_context.exited_containers) {
    if (container->has_role() && container->role() == Role::TABLE) {
      description.emplace_back(GenerateUtteranceByMessageId(MessageIds::EXITED_TABLE));
    } else if (container->has_role() && container->role() == Role::LIST) {
      description.emplace_back(GenerateUtteranceByMessageId(MessageIds::EXITED_LIST));
    }
  }

  // Give hints for entered containers.
  for (auto container : message_context.entered_containers) {
    if (container->has_role() && container->role() == Role::TABLE) {
      description.emplace_back(GenerateUtteranceByMessageId(MessageIds::ENTERED_TABLE));
      DescribeTable(container, description);
    } else if (container->has_role() && container->role() == Role::LIST) {
      DescribeEnteredList(container, description);
    }
  }
}

std::vector<ScreenReaderMessageGenerator::UtteranceAndContext>
ScreenReaderMessageGenerator::DescribeNode(const Node* node,
                                           ScreenReaderMessageContext message_context) {
  // TODO(fxbug.dev/81707): Clean up the logic in this method.
  std::vector<UtteranceAndContext> description;

  DescribeContainerChanges(message_context, description);

  auto role = node->has_role() ? node->role() : Role::UNKNOWN;

  if (role == Role::UNKNOWN && NodeIsSlider(node)) {
    role = Role::SLIDER;
  }

  switch (role) {
    case Role::BUTTON:
      DescribeButton(node, description);
      break;
    case Role::RADIO_BUTTON:
      DescribeRadioButton(node, description);
      break;
    case Role::TOGGLE_SWITCH:
      DescribeToggleSwitch(node, description);
      break;
    case Role::SLIDER:
      DescribeSlider(node, description);
      break;
    case Role::ROW_HEADER:
    case Role::COLUMN_HEADER:
      DescribeRowOrColumnHeader(node, description);
      break;
    case Role::CELL:
      DescribeTableCell(node, std::move(message_context), description);
      break;
    case Role::CHECK_BOX:
      DescribeCheckBox(node, description);
      break;
    case Role::LIST_ELEMENT_MARKER:
      DescribeListElementMarker(node, description);
      break;
    default:
      DescribeTypicalNode(node, description);
  }

  return description;
}

ScreenReaderMessageGenerator::UtteranceAndContext
ScreenReaderMessageGenerator::GenerateUtteranceByMessageId(
    MessageIds message_id, zx::duration delay, const std::vector<std::string>& arg_names,
    const std::vector<i18n::MessageFormatter::ArgValue>& arg_values) {
  UtteranceAndContext utterance;
  auto message = message_formatter_->FormatStringById(static_cast<uint64_t>(message_id), arg_names,
                                                      arg_values);
  if (message != std::nullopt) {
    utterance.utterance.set_message(std::move(*message));
    utterance.delay = delay;
  }
  return utterance;
}

void ScreenReaderMessageGenerator::MaybeAddLabelDescriptor(
    const fuchsia::accessibility::semantics::Node* node,
    std::vector<UtteranceAndContext>& description) {
  FX_DCHECK(node);

  if (node->has_attributes() && node->attributes().has_label() &&
      !node->attributes().label().empty()) {
    Utterance utterance;
    utterance.set_message(node->attributes().label());
    description.emplace_back(UtteranceAndContext{.utterance = std::move(utterance)});
  }
}

void ScreenReaderMessageGenerator::MaybeAddRoleDescriptor(
    const fuchsia::accessibility::semantics::Node* node,
    std::vector<UtteranceAndContext>& description) {
  FX_DCHECK(node);

  if (!node->has_role()) {
    return;
  }

  if (auto message_id = RoleToMessageId(node->role())) {
    description.emplace_back(GenerateUtteranceByMessageId(*message_id));
  }
}

void ScreenReaderMessageGenerator::MaybeAddGenericSelectedDescriptor(
    const fuchsia::accessibility::semantics::Node* node,
    std::vector<UtteranceAndContext>* description) {
  FX_DCHECK(node);
  FX_DCHECK(description);

  if (node->has_states() && node->states().has_selected() && node->states().selected()) {
    description->emplace_back(GenerateUtteranceByMessageId(MessageIds::ELEMENT_SELECTED));
  }
}

void ScreenReaderMessageGenerator::MaybeAddDoubleTapHint(
    const fuchsia::accessibility::semantics::Node* node,
    std::vector<UtteranceAndContext>& description) {
  FX_DCHECK(node);

  if (NodeIsClickable(node)) {
    auto delay = description.empty() ? zx::msec(0) : kLongDelay;

    description.emplace_back(GenerateUtteranceByMessageId(MessageIds::DOUBLE_TAP_HINT, delay));
  }
}

void ScreenReaderMessageGenerator::DescribeTypicalNode(
    const fuchsia::accessibility::semantics::Node* node,
    std::vector<UtteranceAndContext>& description) {
  FX_DCHECK(node);

  MaybeAddGenericSelectedDescriptor(node, &description);
  MaybeAddLabelDescriptor(node, description);
  MaybeAddRoleDescriptor(node, description);
  MaybeAddDoubleTapHint(node, description);
}

void ScreenReaderMessageGenerator::DescribeButton(
    const fuchsia::accessibility::semantics::Node* node,
    std::vector<UtteranceAndContext>& description) {
  FX_DCHECK(node->has_role() && node->role() == fuchsia::accessibility::semantics::Role::BUTTON);

  MaybeAddGenericSelectedDescriptor(node, &description);
  MaybeAddLabelDescriptor(node, description);

  // Announce that the element is a button.
  description.emplace_back(GenerateUtteranceByMessageId(MessageIds::ROLE_BUTTON));

  // Announce the toggled state for the button, if set.
  //
  // Some UI elements have hybrid toggle/button semantics.
  if (node->has_states() && node->states().has_toggled_state()) {
    const auto message_id =
        node->states().toggled_state() == fuchsia::accessibility::semantics::ToggledState::ON
            ? MessageIds::ELEMENT_TOGGLED_ON
            : MessageIds::ELEMENT_TOGGLED_OFF;
    description.emplace_back(GenerateUtteranceByMessageId(message_id));
  }

  MaybeAddDoubleTapHint(node, description);
}

void ScreenReaderMessageGenerator::DescribeRadioButton(
    const fuchsia::accessibility::semantics::Node* node,
    std::vector<UtteranceAndContext>& description) {
  FX_DCHECK(node->has_role() &&
            node->role() == fuchsia::accessibility::semantics::Role::RADIO_BUTTON);

  const auto message_id =
      node->has_states() && node->states().has_selected() && node->states().selected()
          ? MessageIds::RADIO_BUTTON_SELECTED
          : MessageIds::RADIO_BUTTON_UNSELECTED;
  const auto label =
      node->has_attributes() && node->attributes().has_label() ? node->attributes().label() : "";

  // Radio button is a special case: the label is part of the whole message that
  // describes it.
  description.emplace_back(
      GenerateUtteranceByMessageId(message_id, zx::msec(0), {"name"}, {label}));

  MaybeAddDoubleTapHint(node, description);
}

void ScreenReaderMessageGenerator::DescribeCheckBox(
    const fuchsia::accessibility::semantics::Node* node,
    std::vector<UtteranceAndContext>& description) {
  FX_DCHECK(node->has_role() && node->role() == fuchsia::accessibility::semantics::Role::CHECK_BOX);

  MaybeAddLabelDescriptor(node, description);

  description.emplace_back(GenerateUtteranceByMessageId(MessageIds::ROLE_CHECKBOX, kDefaultDelay));

  if (node->has_states() && node->states().has_checked_state() &&
      node->states().checked_state() != fuchsia::accessibility::semantics::CheckedState::NONE) {
    MessageIds message_id = MessageIds::ELEMENT_NOT_CHECKED;
    switch (node->states().checked_state()) {
      case fuchsia::accessibility::semantics::CheckedState::CHECKED:
        message_id = MessageIds::ELEMENT_CHECKED;
        break;
      case fuchsia::accessibility::semantics::CheckedState::UNCHECKED:
        message_id = MessageIds::ELEMENT_NOT_CHECKED;
        break;
      case fuchsia::accessibility::semantics::CheckedState::MIXED:
        message_id = MessageIds::ELEMENT_PARTIALLY_CHECKED;
        break;
      case fuchsia::accessibility::semantics::CheckedState::NONE:
        // When none is present, return without a description of the state.
        return;
    }
    description.emplace_back(GenerateUtteranceByMessageId(message_id));
  }

  MaybeAddRoleDescriptor(node, description);
  MaybeAddDoubleTapHint(node, description);
}

void ScreenReaderMessageGenerator::DescribeListElementMarker(
    const fuchsia::accessibility::semantics::Node* node,
    std::vector<UtteranceAndContext>& description) {
  if (node->has_attributes() && node->attributes().has_label() &&
      !node->attributes().label().empty()) {
    description.push_back(DescribeListElementMarkerLabel(node->attributes().label()));
  }

  MaybeAddRoleDescriptor(node, description);
  MaybeAddDoubleTapHint(node, description);
}

void ScreenReaderMessageGenerator::DescribeToggleSwitch(
    const fuchsia::accessibility::semantics::Node* node,
    std::vector<UtteranceAndContext>& description) {
  FX_DCHECK(node->has_role() &&
            node->role() == fuchsia::accessibility::semantics::Role::TOGGLE_SWITCH);

  MaybeAddLabelDescriptor(node, description);

  const auto message_id =
      node->has_states() && node->states().has_toggled_state() &&
              node->states().toggled_state() == fuchsia::accessibility::semantics::ToggledState::ON
          ? MessageIds::ELEMENT_TOGGLED_ON
          : MessageIds::ELEMENT_TOGGLED_OFF;

  description.emplace_back(GenerateUtteranceByMessageId(message_id));
}

void ScreenReaderMessageGenerator::DescribeSlider(
    const fuchsia::accessibility::semantics::Node* node,
    std::vector<UtteranceAndContext>& description) {
  FX_DCHECK(node);
  FX_DCHECK(NodeIsSlider(node));

  std::string message;
  if (node->has_attributes() && node->attributes().has_label()) {
    message += node->attributes().label();
  }

  const std::string slider_value = GetSliderValue(*node);
  if (!slider_value.empty()) {
    message = message + ", " + slider_value;
  }

  Utterance utterance;
  utterance.set_message(std::move(message));
  description.emplace_back(UtteranceAndContext{.utterance = std::move(utterance)});

  MaybeAddRoleDescriptor(node, description);
  MaybeAddDoubleTapHint(node, description);
}

ScreenReaderMessageGenerator::UtteranceAndContext
ScreenReaderMessageGenerator::DescribeCharacterForSpelling(const std::string& character) {
  const auto it = character_to_message_id_.find(character);
  if (it != character_to_message_id_.end()) {
    return GenerateUtteranceByMessageId(it->second);
  }

  // TODO(fxbug.dev/89506): Logic to detect uppercase letters may lead to bugs in non English
  // locales. Checks if this character is uppercase.
  if (character.size() == 1 && std::isupper(character[0])) {
    return GenerateUtteranceByMessageId(MessageIds::CAPITALIZED_LETTER, zx::msec(0), {"letter"},
                                        {character});
  }

  UtteranceAndContext utterance;
  utterance.utterance.set_message(character);
  return utterance;
}

ScreenReaderMessageGenerator::UtteranceAndContext
ScreenReaderMessageGenerator::DescribeListElementMarkerLabel(const std::string& label) {
  const auto trimmed_label = std::string(fxl::TrimString(label, " \t"));
  const auto it = character_to_message_id_.find(trimmed_label);
  if (it != character_to_message_id_.end()) {
    return GenerateUtteranceByMessageId(it->second);
  }

  UtteranceAndContext utterance;
  utterance.utterance.set_message(label);
  return utterance;
}

void ScreenReaderMessageGenerator::DescribeTable(
    const fuchsia::accessibility::semantics::Node* node,
    std::vector<UtteranceAndContext>& description) {
  FX_DCHECK(node->has_role() && node->role() == fuchsia::accessibility::semantics::Role::TABLE);

  if (node->has_attributes()) {
    const auto& attributes = node->attributes();

    // Add the table label to the description.
    std::string label;
    if (attributes.has_label() && !attributes.label().empty()) {
      label = attributes.label();
      Utterance utterance;
      utterance.set_message(label);
      description.emplace_back(UtteranceAndContext{.utterance = std::move(utterance)});
    }

    // Add the table dimensions to the description.
    if (attributes.has_table_attributes()) {
      const auto& table_attributes = attributes.table_attributes();

      // The table dimensions will only make sense if we have both the number of rows and the
      // number of columns.
      if (table_attributes.has_number_of_rows() && table_attributes.has_number_of_columns()) {
        auto num_rows = std::to_string(table_attributes.number_of_rows());
        auto num_columns = std::to_string(table_attributes.number_of_columns());
        description.emplace_back(
            GenerateUtteranceByMessageId(MessageIds::TABLE_DIMENSIONS, zx::msec(0),
                                         {"num_rows", "num_columns"}, {num_rows, num_columns}));
      }
    }
  }
}

void ScreenReaderMessageGenerator::DescribeTableCell(
    const fuchsia::accessibility::semantics::Node* node, ScreenReaderMessageContext message_context,
    std::vector<UtteranceAndContext>& description) {
  FX_DCHECK(node->has_role() && node->role() == fuchsia::accessibility::semantics::Role::CELL);

  if (node->has_attributes()) {
    const auto& attributes = node->attributes();

    // Add the cell label to the description.
    std::string label;
    if (attributes.has_label() && !attributes.label().empty()) {
      if (message_context.changed_table_cell_context) {
        // The message context will only have the row/column header fields
        // populated if the user has navigated to a new row/column since the
        // last cell was read. So, we can add them to the description unconditionally
        // here if they are present.
        if (!message_context.changed_table_cell_context->row_header.empty()) {
          label += message_context.changed_table_cell_context->row_header + ", ";
        }

        if (!message_context.changed_table_cell_context->column_header.empty()) {
          label += message_context.changed_table_cell_context->column_header + ", ";
        }
      }

      label += attributes.label();

      Utterance utterance;
      utterance.set_message(label);
      description.emplace_back(UtteranceAndContext{.utterance = std::move(utterance)});
    }

    // Add the cell row/column spans and row/column indices to the description.
    if (attributes.has_table_cell_attributes()) {
      const auto& table_cell_attributes = attributes.table_cell_attributes();

      // We only want to speak the row span if it's > 1.
      if (table_cell_attributes.has_row_span() && table_cell_attributes.row_span() > 1) {
        auto row_span = std::to_string(table_cell_attributes.row_span());
        description.emplace_back(GenerateUtteranceByMessageId(MessageIds::ROW_SPAN, zx::msec(0),
                                                              {"row_span"}, {row_span}));
      }

      // We only want to speak the column span if it's > 1.
      if (table_cell_attributes.has_column_span() && table_cell_attributes.column_span() > 1) {
        auto column_span = std::to_string(table_cell_attributes.column_span());
        description.emplace_back(GenerateUtteranceByMessageId(MessageIds::COLUMN_SPAN, zx::msec(0),
                                                              {"column_span"}, {column_span}));
      }

      if (table_cell_attributes.has_row_index() && table_cell_attributes.has_column_index()) {
        // We want to announce them as 1-indexed.
        auto row_index = std::to_string(table_cell_attributes.row_index() + 1);
        auto column_index = std::to_string(table_cell_attributes.column_index() + 1);
        description.emplace_back(GenerateUtteranceByMessageId(MessageIds::CELL_SUMMARY, zx::msec(0),
                                                              {"row_index", "column_index"},
                                                              {row_index, column_index}));
      }
    }
  }

  description.emplace_back(GenerateUtteranceByMessageId(MessageIds::ROLE_TABLE_CELL));

  MaybeAddRoleDescriptor(node, description);
  MaybeAddDoubleTapHint(node, description);
}

void ScreenReaderMessageGenerator::DescribeRowOrColumnHeader(
    const fuchsia::accessibility::semantics::Node* node,
    std::vector<UtteranceAndContext>& description) {
  FX_DCHECK(node->has_role() &&
            (node->role() == fuchsia::accessibility::semantics::Role::ROW_HEADER ||
             node->role() == fuchsia::accessibility::semantics::Role::COLUMN_HEADER));

  if (node->has_attributes()) {
    const auto& attributes = node->attributes();

    // Add the label to the description.
    std::string label;
    if (attributes.has_label() && !attributes.label().empty()) {
      Utterance utterance;
      utterance.set_message(attributes.label());
      description.emplace_back(UtteranceAndContext{.utterance = std::move(utterance)});
    }

    if (attributes.has_table_cell_attributes()) {
      const auto& table_cell_attributes = attributes.table_cell_attributes();

      // Add the row/column index to the description. Note that only one of
      // these should be set, depending on whether this header is a row or a
      // column header.
      if (table_cell_attributes.has_row_index()) {
        // Row index should only be set for a row header.
        FX_DCHECK(node->role() == fuchsia::accessibility::semantics::Role::ROW_HEADER);
        // We want to announce it as 1-indexed.
        auto row_index = std::to_string(table_cell_attributes.row_index() + 1);
        description.emplace_back(GenerateUtteranceByMessageId(MessageIds::ROW_SUMMARY, zx::msec(0),
                                                              {"row_index"}, {row_index}));
      }

      if (table_cell_attributes.has_column_index()) {
        // Column index should only be set for a column header.
        FX_DCHECK(node->role() == fuchsia::accessibility::semantics::Role::COLUMN_HEADER);
        // We want to announce it as 1-indexed.
        auto column_index = std::to_string(table_cell_attributes.column_index() + 1);
        description.emplace_back(GenerateUtteranceByMessageId(
            MessageIds::COLUMN_SUMMARY, zx::msec(0), {"column_index"}, {column_index}));
      }

      // Add the row/column span to the description.
      if (table_cell_attributes.has_row_span() && table_cell_attributes.row_span() > 1) {
        auto row_span = std::to_string(table_cell_attributes.row_span());
        description.emplace_back(GenerateUtteranceByMessageId(MessageIds::ROW_SPAN, zx::msec(0),
                                                              {"row_span"}, {row_span}));
      }

      if (table_cell_attributes.has_column_span() && table_cell_attributes.column_span() > 1) {
        auto column_span = std::to_string(table_cell_attributes.column_span());
        description.emplace_back(GenerateUtteranceByMessageId(MessageIds::COLUMN_SPAN, zx::msec(0),
                                                              {"column_span"}, {column_span}));
      }
    }
  }

  if (node->role() == fuchsia::accessibility::semantics::Role::ROW_HEADER) {
    description.emplace_back(GenerateUtteranceByMessageId(MessageIds::ROLE_TABLE_ROW_HEADER));
  } else {
    description.emplace_back(GenerateUtteranceByMessageId(MessageIds::ROLE_TABLE_COLUMN_HEADER));
  }

  MaybeAddRoleDescriptor(node, description);
  MaybeAddDoubleTapHint(node, description);
}

void ScreenReaderMessageGenerator::DescribeEnteredList(
    const fuchsia::accessibility::semantics::Node* node,
    std::vector<UtteranceAndContext>& description) {
  FX_DCHECK(node->has_role() && node->role() == fuchsia::accessibility::semantics::Role::LIST);

  if (node->has_attributes() && node->attributes().has_list_attributes() &&
      node->attributes().list_attributes().has_size()) {
    description.emplace_back(
        GenerateUtteranceByMessageId(MessageIds::ENTERED_LIST_DETAIL, zx::msec(0), {"num_items"},
                                     {node->attributes().list_attributes().size()}));
  } else {
    description.emplace_back(GenerateUtteranceByMessageId(MessageIds::ENTERED_LIST));
  }

  // Add the list label to the description, if it's present.
  if (node->has_attributes() && node->attributes().has_label() &&
      !node->attributes().label().empty()) {
    Utterance utterance;
    utterance.set_message(node->attributes().label());
    description.emplace_back(UtteranceAndContext{.utterance = std::move(utterance)});
  }
}

}  // namespace a11y
