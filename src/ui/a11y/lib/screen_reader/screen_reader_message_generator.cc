// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>

#include <optional>

#include "fuchsia/accessibility/semantics/cpp/fidl.h"
// This header file has been generated from the strings library fuchsia.intl.l10n.
#include "fuchsia/intl/l10n/cpp/fidl.h"
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

// Returns a message that describes the label and range value attributes.
std::string GetSliderLabelAndRangeMessage(const fuchsia::accessibility::semantics::Node* node) {
  FX_DCHECK(node);

  std::string message;
  if (node->has_attributes() && node->attributes().has_label()) {
    message += node->attributes().label();
  }

  std::string slider_value = GetSliderValue(*node);
  if (!slider_value.empty()) {
    message = message + ", " + slider_value;
  }

  return message;
}

// Returns true if the node is clickable in any way (normal click, long click).
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

std::vector<ScreenReaderMessageGenerator::UtteranceAndContext>
ScreenReaderMessageGenerator::DescribeContainer(ScreenReaderMessageContext message_context) {
  std::vector<UtteranceAndContext> description;

  if (message_context.current_container != message_context.previous_container) {
    // If the current container is null, then the previous container must NOT be
    // null, so the user has exited a container. It's also possible that the
    // current container was a descendant of the previous container, in
    // which case we only want to announce that we've exited the previous
    // container.
    //
    // Otherwise, we must have entered a new container.
    if (!message_context.current_container ||
        (message_context.previous_container && message_context.exited_nested_container)) {
      if (message_context.previous_container->has_role() &&
          message_context.previous_container->role() == Role::TABLE) {
        description.emplace_back(GenerateUtteranceByMessageId(MessageIds::EXITED_TABLE));
      }
    } else {
      if (message_context.current_container->has_role() &&
          message_context.current_container->role() == Role::TABLE) {
        description.emplace_back(GenerateUtteranceByMessageId(MessageIds::ENTERED_TABLE));
        auto container_description = DescribeTable(message_context.current_container);
        std::copy(std::make_move_iterator(container_description.begin()),
                  std::make_move_iterator(container_description.end()),
                  std::back_inserter(description));
      }
    }
  }

  return description;
}

std::vector<ScreenReaderMessageGenerator::UtteranceAndContext>
ScreenReaderMessageGenerator::DescribeNode(const Node* node,
                                           ScreenReaderMessageContext message_context) {
  // TODO(fxbug.dev/81707): Clean up the logic in this method.
  std::vector<UtteranceAndContext> description;
  if (message_context.HasDescribableContainer()) {
    description = DescribeContainer(message_context);
  }

  {
    const std::string label = node->has_attributes() && node->attributes().has_label() &&
                                      !node->attributes().label().empty()
                                  ? node->attributes().label()
                                  : "";

    // If this node is a radio button, slider-like object, or toggle switch, the label is part of
    // the whole message that describes it.
    if (node->has_role() && node->role() == fuchsia::accessibility::semantics::Role::RADIO_BUTTON) {
      description.emplace_back(DescribeRadioButton(node));
    } else if (node->has_role() &&
               node->role() == fuchsia::accessibility::semantics::Role::TOGGLE_SWITCH) {
      if (!label.empty()) {
        Utterance utterance;
        utterance.set_message(label);
        description.emplace_back(UtteranceAndContext{.utterance = std::move(utterance)});
      }
      description.emplace_back(DescribeToggleSwitch(node));
    } else if ((node->has_states() && node->states().has_range_value()) ||
               (node->has_role() &&
                node->role() == fuchsia::accessibility::semantics::Role::SLIDER)) {
      Utterance utterance;
      utterance.set_message(GetSliderLabelAndRangeMessage(node));
      description.emplace_back(UtteranceAndContext{.utterance = std::move(utterance)});
    } else if (node->has_role() &&
               (node->role() == Role::ROW_HEADER || node->role() == Role::COLUMN_HEADER)) {
      auto header_description = DescribeRowOrColumnHeader(node);
      std::copy(std::make_move_iterator(header_description.begin()),
                std::make_move_iterator(header_description.end()), std::back_inserter(description));
    } else if (node->has_role() && node->role() == Role::CELL) {
      auto cell_description = DescribeTableCell(node, std::move(message_context));
      std::copy(std::make_move_iterator(cell_description.begin()),
                std::make_move_iterator(cell_description.end()), std::back_inserter(description));
    } else if (!label.empty()) {
      Utterance utterance;
      utterance.set_message(label);
      description.emplace_back(UtteranceAndContext{.utterance = std::move(utterance)});
    }
  }

  {
    Utterance utterance;
    if (node->has_role()) {
      if (node->role() == Role::BUTTON) {
        description.emplace_back(GenerateUtteranceByMessageId(MessageIds::ROLE_BUTTON));
      } else if (node->role() == Role::HEADER) {
        description.emplace_back(GenerateUtteranceByMessageId(MessageIds::ROLE_HEADER));
      } else if (node->role() == Role::IMAGE) {
        description.emplace_back(GenerateUtteranceByMessageId(MessageIds::ROLE_IMAGE));
      } else if (node->role() == Role::LINK) {
        description.emplace_back(GenerateUtteranceByMessageId(MessageIds::ROLE_LINK));
      } else if (node->role() == Role::TEXT_FIELD) {
        description.emplace_back(GenerateUtteranceByMessageId(MessageIds::ROLE_TEXT_FIELD));
      } else if (node->role() == Role::SEARCH_BOX) {
        description.emplace_back(GenerateUtteranceByMessageId(MessageIds::ROLE_SEARCH_BOX));
      } else if (node->role() == Role::CHECK_BOX) {
        auto check_box_description = DescribeCheckBox(node);
        std::copy(std::make_move_iterator(check_box_description.begin()),
                  std::make_move_iterator(check_box_description.end()),
                  std::back_inserter(description));
      } else if (node->role() == Role::SLIDER) {
        description.emplace_back(GenerateUtteranceByMessageId(MessageIds::ROLE_SLIDER));
      }
    }
  }
  if (NodeIsClickable(node)) {
    description.emplace_back(GenerateUtteranceByMessageId(MessageIds::DOUBLE_TAP_HINT, kLongDelay));
  }

  return description;
}

ScreenReaderMessageGenerator::UtteranceAndContext
ScreenReaderMessageGenerator::GenerateUtteranceByMessageId(
    MessageIds message_id, zx::duration delay, const std::vector<std::string>& arg_names,
    const std::vector<std::string>& arg_values) {
  UtteranceAndContext utterance;
  auto message = message_formatter_->FormatStringById(static_cast<uint64_t>(message_id), arg_names,
                                                      arg_values);
  if (message != std::nullopt) {
    utterance.utterance.set_message(std::move(*message));
    utterance.delay = delay;
  }
  return utterance;
}

ScreenReaderMessageGenerator::UtteranceAndContext ScreenReaderMessageGenerator::DescribeRadioButton(
    const fuchsia::accessibility::semantics::Node* node) {
  FX_DCHECK(node->has_role() &&
            node->role() == fuchsia::accessibility::semantics::Role::RADIO_BUTTON);
  const auto message_id =
      node->has_states() && node->states().has_selected() && node->states().selected()
          ? MessageIds::RADIO_BUTTON_SELECTED
          : MessageIds::RADIO_BUTTON_UNSELECTED;
  const auto name_value =
      node->has_attributes() && node->attributes().has_label() ? node->attributes().label() : "";
  if (!name_value.empty()) {
    return GenerateUtteranceByMessageId(message_id, zx::duration(zx::msec(0)), {"name"},
                                        {name_value});
  }

  return GenerateUtteranceByMessageId(message_id);
}

std::vector<ScreenReaderMessageGenerator::UtteranceAndContext>
ScreenReaderMessageGenerator::DescribeCheckBox(
    const fuchsia::accessibility::semantics::Node* node) {
  FX_DCHECK(node->has_role() && node->role() == fuchsia::accessibility::semantics::Role::CHECK_BOX);
  std::vector<ScreenReaderMessageGenerator::UtteranceAndContext> description;
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
        return description;
    }
    description.emplace_back(GenerateUtteranceByMessageId(message_id));
  }
  return description;
}

ScreenReaderMessageGenerator::UtteranceAndContext
ScreenReaderMessageGenerator::DescribeToggleSwitch(
    const fuchsia::accessibility::semantics::Node* node) {
  FX_DCHECK(node->has_role() &&
            node->role() == fuchsia::accessibility::semantics::Role::TOGGLE_SWITCH);
  const auto message_id =
      node->has_states() && node->states().has_toggled_state() &&
              node->states().toggled_state() == fuchsia::accessibility::semantics::ToggledState::ON
          ? MessageIds::ELEMENT_TOGGLED_ON
          : MessageIds::ELEMENT_TOGGLED_OFF;
  return GenerateUtteranceByMessageId(message_id, zx::duration(zx::msec(0)));
}

ScreenReaderMessageGenerator::UtteranceAndContext
ScreenReaderMessageGenerator::FormatCharacterForSpelling(const std::string& character) {
  const auto it = character_to_message_id_.find(character);
  if (it != character_to_message_id_.end()) {
    return GenerateUtteranceByMessageId(it->second);
  }

  UtteranceAndContext utterance;

  // TODO(fxbug.dev/89506): Logic to detect uppercase letters may lead to bugs in non English
  // locales. Checks if this character is uppercase.
  if (character.size() == 1 && std::isupper(character[0])) {
    return GenerateUtteranceByMessageId(MessageIds::CAPITALIZED_LETTER, zx::msec(0), {"letter"},
                                        {character});
  }

  utterance.utterance.set_message(character);
  return utterance;
}

std::vector<ScreenReaderMessageGenerator::UtteranceAndContext>
ScreenReaderMessageGenerator::DescribeTable(const fuchsia::accessibility::semantics::Node* node) {
  FX_DCHECK(node->has_role() && node->role() == fuchsia::accessibility::semantics::Role::TABLE);

  std::vector<ScreenReaderMessageGenerator::UtteranceAndContext> description;

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

      // The table dimensions will only make sense if we have both the number of rows and the number
      // of columns.
      if (table_attributes.has_number_of_rows() && table_attributes.has_number_of_columns()) {
        auto num_rows = std::to_string(table_attributes.number_of_rows());
        auto num_columns = std::to_string(table_attributes.number_of_columns());
        description.emplace_back(
            GenerateUtteranceByMessageId(MessageIds::TABLE_DIMENSIONS, zx::duration(zx::msec(0)),
                                         {"num_rows", "num_columns"}, {num_rows, num_columns}));
      }
    }
  }

  description.emplace_back(GenerateUtteranceByMessageId(MessageIds::ROLE_TABLE));

  return description;
}

std::vector<ScreenReaderMessageGenerator::UtteranceAndContext>
ScreenReaderMessageGenerator::DescribeTableCell(const fuchsia::accessibility::semantics::Node* node,
                                                ScreenReaderMessageContext message_context) {
  FX_DCHECK(node->has_role() && node->role() == fuchsia::accessibility::semantics::Role::CELL);

  std::vector<ScreenReaderMessageGenerator::UtteranceAndContext> description;

  if (node->has_attributes()) {
    const auto& attributes = node->attributes();

    // Add the cell label to the description.
    std::string label;
    if (attributes.has_label() && !attributes.label().empty()) {
      if (message_context.table_cell_context) {
        // The message context will only have the row/column header fields
        // populated if the user has navigated to a new row/column since the
        // last cell was read. So, we can add them to the description unconditionally
        // here if they are present.
        if (!message_context.table_cell_context->row_header.empty()) {
          label += message_context.table_cell_context->row_header + ", ";
        }

        if (!message_context.table_cell_context->column_header.empty()) {
          label += message_context.table_cell_context->column_header + ", ";
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
        description.emplace_back(GenerateUtteranceByMessageId(
            MessageIds::ROW_SPAN, zx::duration(zx::msec(0)), {"row_span"}, {row_span}));
      }

      // We only want to speak the column span if it's > 1.
      if (table_cell_attributes.has_column_span() && table_cell_attributes.column_span() > 1) {
        auto column_span = std::to_string(table_cell_attributes.column_span());
        description.emplace_back(GenerateUtteranceByMessageId(
            MessageIds::COLUMN_SPAN, zx::duration(zx::msec(0)), {"column_span"}, {column_span}));
      }

      if (table_cell_attributes.has_row_index() && table_cell_attributes.has_column_index()) {
        auto row_index = std::to_string(table_cell_attributes.row_index());
        auto column_index = std::to_string(table_cell_attributes.column_index());
        description.emplace_back(
            GenerateUtteranceByMessageId(MessageIds::CELL_SUMMARY, zx::duration(zx::msec(0)),
                                         {"row_index", "column_index"}, {row_index, column_index}));
      }
    }
  }

  description.emplace_back(GenerateUtteranceByMessageId(MessageIds::ROLE_TABLE_CELL));

  return description;
}

std::vector<ScreenReaderMessageGenerator::UtteranceAndContext>
ScreenReaderMessageGenerator::DescribeRowOrColumnHeader(
    const fuchsia::accessibility::semantics::Node* node) {
  FX_DCHECK(node->has_role() &&
            (node->role() == fuchsia::accessibility::semantics::Role::ROW_HEADER ||
             node->role() == fuchsia::accessibility::semantics::Role::COLUMN_HEADER));

  std::vector<ScreenReaderMessageGenerator::UtteranceAndContext> description;

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
        auto row_index = std::to_string(table_cell_attributes.row_index());
        description.emplace_back(GenerateUtteranceByMessageId(
            MessageIds::ROW_SUMMARY, zx::duration(zx::msec(0)), {"row_index"}, {row_index}));
      }

      if (table_cell_attributes.has_column_index()) {
        // Column index should only be set for a column header.
        FX_DCHECK(node->role() == fuchsia::accessibility::semantics::Role::COLUMN_HEADER);
        auto column_index = std::to_string(table_cell_attributes.column_index());
        description.emplace_back(GenerateUtteranceByMessageId(MessageIds::COLUMN_SUMMARY,
                                                              zx::duration(zx::msec(0)),
                                                              {"column_index"}, {column_index}));
      }

      // Add the row/column span to the description.
      if (table_cell_attributes.has_row_span() && table_cell_attributes.row_span() > 1) {
        auto row_span = std::to_string(table_cell_attributes.row_span());
        description.emplace_back(GenerateUtteranceByMessageId(
            MessageIds::ROW_SPAN, zx::duration(zx::msec(0)), {"row_span"}, {row_span}));
      }

      if (table_cell_attributes.has_column_span() && table_cell_attributes.column_span() > 1) {
        auto column_span = std::to_string(table_cell_attributes.column_span());
        description.emplace_back(GenerateUtteranceByMessageId(
            MessageIds::COLUMN_SPAN, zx::duration(zx::msec(0)), {"column_span"}, {column_span}));
      }
    }
  }

  if (node->role() == fuchsia::accessibility::semantics::Role::ROW_HEADER) {
    description.emplace_back(GenerateUtteranceByMessageId(MessageIds::ROLE_TABLE_ROW_HEADER));
  } else {
    description.emplace_back(GenerateUtteranceByMessageId(MessageIds::ROLE_TABLE_COLUMN_HEADER));
  }

  return description;
}

}  // namespace a11y
