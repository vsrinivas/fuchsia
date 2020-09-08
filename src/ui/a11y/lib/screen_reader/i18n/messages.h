// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SCREEN_READER_I18N_MESSAGES_H_
#define SRC_UI_A11Y_LIB_SCREEN_READER_I18N_MESSAGES_H_

#include <string_view>

namespace a11y {
namespace i18n {

// Represents a message that can be translated.
struct LocalizedMessage {
  // Unique ID for this message.
  std::string_view id;
  // ICU MessageFormat pattern for this message.
  // For documentation, see:
  // https://unicode-org.github.io/icu-docs/apidoc/released/icu4c/classicu_1_1MessageFormat.html
  std::string_view default_message;
};

#define DEF_MESSAGE(m_id, m_default_message)                           \
  inline constexpr ::a11y::i18n::LocalizedMessage m_id = {.id = #m_id, \
                                                          .default_message = m_default_message};

// Spoken message that describes a heading. For example, an element with the ARIA role heading.
DEF_MESSAGE(role_header, "Heading");

// Spoken message that describes a button. For example, an element with the ARIA role button.
DEF_MESSAGE(role_button, "Button");

// Spoken message that Describes a check box. For example, an element with the ARIA role checkbox.
DEF_MESSAGE(role_checkbox, "Check box");

// Spoken message that Describes a combo box. For example, an element with the ARIA role combobox.
DEF_MESSAGE(role_combobox, "Combo box");

// Spoken message that Describes an image. For example, an element with the ARIA role img.
DEF_MESSAGE(role_image, "Image");

// Spoken message that Describes a link in a document or in a page.
DEF_MESSAGE(role_link, "Link");

// Spoken message that Describes a progress bar. For example, an element with the ARIA role
// progressbar.
DEF_MESSAGE(role_progressbar, "Progress bar");

// Spoken message that Describes a slider. For example, an element with the ARIA role slider.
DEF_MESSAGE(role_slider, "Slider");

// Spoken message that Describes a tab. For example, an element with the ARIA role tab.
DEF_MESSAGE(role_tab, "Tab");

// Spoken message that Describes a radio button. For example, an element with the ARIA role radio.
DEF_MESSAGE(role_radiobutton, "Radio button");

// Describes a HTML radio button named |name| in the selected state.
DEF_MESSAGE(radio_button_selected, "{name}, radio button selected");

// Describes a HTML radio button named |name| in the unselected state.
DEF_MESSAGE(radio_button_unselected, "{name}, radio button unselected");

// Spoken message that describes a table. For example, a <table> html tag.
DEF_MESSAGE(role_table, "Table");

// Spoken message that summarizes a table.
DEF_MESSAGE(table_summary, "{table_name}, {table_rows} by {table_columns}");

// Spoken message that summarizes a table cell.
DEF_MESSAGE(cell_summary, "Row {row_index} column {column_index}");

// Spoken message that describes an element that is checked. For example, a check box is checked in
// a html form.
DEF_MESSAGE(element_checked, "Checked");

// Spoken message that describes an element that is not checked. For example, a check box is not
// checked in a html form.
DEF_MESSAGE(element_not_checked, "Not checked");

// Spoken message that describes an element where the checked state is mixed or indeterminate.
DEF_MESSAGE(element_partially_checked, "Partially checked");

// Spoken message that describes an element that is disabled. For example, an element with the ARIA
// attribute aria-disabled=true.
DEF_MESSAGE(element_disabled, "Disabled");

// Spoken message that describes an element that is expanded. For example, an element with the ARIA
// attribute aria-expanded=true.
DEF_MESSAGE(element_expanded, "Expanded");

// Spoken message that describes an element that is collapsed. For example, an element with the ARIA
// attribute aria-expanded=false.
DEF_MESSAGE(element_collapsed, "Collapsed");

// Spoken message that describes a <h1> html tag.
DEF_MESSAGE(heading_level_1, "Heading 1");

// Spoken message that describes a <h2> html tag.
DEF_MESSAGE(heading_level_2, "Heading 2");

// Spoken message that describes a <h3> html tag.
DEF_MESSAGE(heading_level_3, "Heading 3");

// Spoken message that describes a <h4> html tag.
DEF_MESSAGE(heading_level_4, "Heading 4");

// Spoken message that describes a <h5> html tag.
DEF_MESSAGE(heading_level_5, "Heading 5");

// Spoken message that describes a <h6> html tag.
DEF_MESSAGE(heading_level_6, "Heading 6");

// Spoken message that describes a list item. For example, a <li> html tag.
DEF_MESSAGE(role_list_item, "List item");

// Spoken message that describes a ordered list. For example, a <ol> html tag.
DEF_MESSAGE(role_ordered_list, "Ordered list");

// Spoken message that describes a list. For example, <ul> html tag.
DEF_MESSAGE(role_list, "List");

// Spoken message that describes a list-like element in detail. For example, this message can be
// combined with others to produce List with 3 items.
DEF_MESSAGE(list_items,
            "{num_items, plural, =0{with no items} =1{with one item} other{with # items}}");

// Spoken message that describes when the user switches to the normal navigation granularity, which
// allows one to navigate the interface visiting all focusable elements.
DEF_MESSAGE(normal_navigation_granularity, "Normal navigation");

// Spoken message that describes when the user switches to the default navigation granularity, which
// allows one to navigate the interface visiting all focusable elements.
DEF_MESSAGE(default_navigation_granularity, "Default");

// Spoken message that describes when the user switches to the adjust value granularity, which
// allows one to control a slider value.
DEF_MESSAGE(adjust_value_granularity, "Adjust value");

// Spoken message that describes when the user switches to the heading granularity, which allows one
// to navigate the interface one heading at a time.
DEF_MESSAGE(heading_granularity, "Heading");

// Spoken message that describes when the user switches to the form control granularity, which
// allows one to navigate the interface one form control at a time.
DEF_MESSAGE(form_control_granularity, "Form control");

// Spoken message that describes when the user switches to the link granularity, which allows one to
// navigate the interface one link at a time.
DEF_MESSAGE(link_granularity, "Link");

// Spoken message that describes when the user switches to the line granularity, which allows one to
// navigate the interface one line at a time.
DEF_MESSAGE(line_granularity, "Line");

// Spoken message that describes when the user switches to the word granularity, which allows one to
// navigate the interface one word at a time.
DEF_MESSAGE(word_granularity, "Word");

// Spoken message that describes when the user switches to the character granularity, which allows
// one to navigate the interface one character at a time.
DEF_MESSAGE(character_granularity, "Character");

// Spoken message to the user that a control can be double tapped to be activated.
DEF_MESSAGE(double_tap_hint, "Double tap to activate");

// Spoken message to the user that a control's value can be adjusted.
DEF_MESSAGE(adjust_value_hint, "Swipe left or right to adjust the value.");

// Spoken message that describes that an element is the last of its type.
DEF_MESSAGE(last_element, "Last");

// Spoken message that describes that an element is the first of its type.
DEF_MESSAGE(first_element, "First");

// Spoken message to alert the user that the Screen Reader has no current focus when using a touch
// screen.
DEF_MESSAGE(no_focus_alert, "No focus. Touch and explore to find items.");

// Spoken message to alert the user that the Screen Reader, an assistive technology software used by
// the blindn and visually impaired to access computers or smartphones, is on.
DEF_MESSAGE(screen_reader_on_hint, "Screen Reader on.");

// Spoken message to alert the user that the Screen Reader, an assistive technology software used by
// the blindn and visually impaired to access computers or smartphones, is off.
DEF_MESSAGE(screen_reader_off_hint, "Screen Reader off.");

}  // namespace i18n
}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_SCREEN_READER_I18N_MESSAGES_H_
