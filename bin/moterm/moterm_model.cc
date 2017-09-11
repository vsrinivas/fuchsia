// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/moterm/moterm_model.h"

#include <string.h>

#include <algorithm>
#include <limits>

#include "lib/fxl/logging.h"

namespace {

// Moterm -> teken conversions:

teken_pos_t MotermToTekenSize(const MotermModel::Size& size) {
  FXL_DCHECK(size.rows <= std::numeric_limits<teken_unit_t>::max());
  FXL_DCHECK(size.columns <= std::numeric_limits<teken_unit_t>::max());
  teken_pos_t rv = {static_cast<teken_unit_t>(size.rows),
                    static_cast<teken_unit_t>(size.columns)};
  return rv;
}

// Teken -> moterm conversions:

MotermModel::Position TekenToMotermPosition(const teken_pos_t& position) {
  return MotermModel::Position(static_cast<int>(position.tp_row),
                               static_cast<int>(position.tp_col));
}

MotermModel::Size TekenToMotermSize(const teken_pos_t& size) {
  return MotermModel::Size(size.tp_row, size.tp_col);
}

MotermModel::Rectangle TekenToMotermRectangle(const teken_rect_t& rectangle) {
  return MotermModel::Rectangle(
      static_cast<int>(rectangle.tr_begin.tp_row),
      static_cast<int>(rectangle.tr_begin.tp_col),
      rectangle.tr_end.tp_row - rectangle.tr_begin.tp_row,
      rectangle.tr_end.tp_col - rectangle.tr_begin.tp_col);
}

MotermModel::Color TekenToMotermColor(teken_color_t color, bool bold) {
  static const uint8_t rgb[TC_NCOLORS][3] = {
      {0x00, 0x00, 0x00},  // Black.
      {0x80, 0x00, 0x00},  // Red.
      {0x00, 0x80, 0x00},  // Green.
      {0x80, 0x80, 0x00},  // Yellow (even if teken thinks it's brown).
      {0x00, 0x00, 0x80},  // Blue.
      {0x80, 0x00, 0x80},  // Magenta.
      {0x00, 0x80, 0x80},  // Cyan.
      {0xc0, 0xc0, 0xc0}   // White.
  };
  static const uint8_t bold_rgb[TC_NCOLORS][3] = {
      {0x80, 0x80, 0x80},  // Black.
      {0xff, 0x00, 0x00},  // Red.
      {0x00, 0xff, 0x00},  // Green.
      {0xff, 0xff, 0x00},  // Yellow (even if teken thinks it's brown).
      {0x00, 0x00, 0xff},  // Blue.
      {0xff, 0x00, 0xff},  // Magenta.
      {0x00, 0xff, 0xff},  // Cyan.
      {0xff, 0xff, 0xff}   // White.
  };
  FXL_DCHECK(color < static_cast<unsigned>(TC_NCOLORS));
  return bold ? MotermModel::Color(bold_rgb[color][0], bold_rgb[color][1],
                                   bold_rgb[color][2])
              : MotermModel::Color(rgb[color][0], rgb[color][1], rgb[color][2]);
}

// Utility functions:

MotermModel::Rectangle EnclosingRectangle(const MotermModel::Rectangle& rect1,
                                          const MotermModel::Rectangle& rect2) {
  if (rect1.IsEmpty())
    return rect2;
  if (rect2.IsEmpty())
    return rect1;

  int start_row = std::min(rect1.position.row, rect2.position.row);
  int start_col = std::min(rect1.position.column, rect2.position.column);
  // TODO(vtl): Some theoretical overflows here.
  int end_row =
      std::max(rect1.position.row + static_cast<int>(rect1.size.rows),
               rect2.position.row + static_cast<int>(rect2.size.rows));
  int end_col =
      std::max(rect1.position.column + static_cast<int>(rect1.size.columns),
               rect2.position.column + static_cast<int>(rect2.size.columns));
  FXL_DCHECK(start_row <= end_row);
  FXL_DCHECK(start_col <= end_col);
  return MotermModel::Rectangle(start_row, start_col,
                                static_cast<unsigned>(end_row - start_row),
                                static_cast<unsigned>(end_col - start_col));
}

}  // namespace

const MotermModel::Attributes MotermModel::kAttributesBold;
const MotermModel::Attributes MotermModel::kAttributesUnderline;
const MotermModel::Attributes MotermModel::kAttributesBlink;

MotermModel::MotermModel(const Size& size, Delegate* delegate)
    : size_(size),
      delegate_(delegate),
      cursor_visible_(true),
      terminal_(),
      current_state_changes_() {
  FXL_DCHECK(size_.rows > 0u);
  FXL_DCHECK(size_.columns > 0u);

  num_chars_ = size_.rows * size_.columns;
  characters_.reset(new teken_char_t[num_chars_]);
  memset(characters_.get(), 0, num_chars_ * sizeof(teken_char_t));
  attributes_.reset(new teken_attr_t[num_chars_]);
  memset(attributes_.get(), 0, num_chars_ * sizeof(teken_attr_t));

  static const teken_funcs_t callbacks = {
      &MotermModel::OnBellThunk,    &MotermModel::OnCursorThunk,
      &MotermModel::OnPutcharThunk, &MotermModel::OnFillThunk,
      &MotermModel::OnCopyThunk,    &MotermModel::OnParamThunk,
      &MotermModel::OnRespondThunk};
  teken_init(&terminal_, &callbacks, this);

  teken_pos_t s = MotermToTekenSize(size_);
  teken_set_winsize(&terminal_, &s);
}

MotermModel::~MotermModel() {}

void MotermModel::ProcessInput(const void* input_bytes,
                               size_t num_input_bytes,
                               StateChanges* state_changes) {
  FXL_DCHECK(state_changes);
  FXL_DCHECK(!current_state_changes_);
  current_state_changes_ = state_changes;

  // Get the initial cursor position, so we'll be able to tell if it moved.
  teken_pos_t initial_cursor_pos = *teken_get_cursor(&terminal_);

  // Note: This may call some of our callbacks.
  char cr = '\r';
  const char* buffer = static_cast<const char*>(input_bytes);
  for (size_t i = 0; i < num_input_bytes; i++) {
    if (*buffer == '\n') {
      teken_input(&terminal_, &cr, 1);
    }
    teken_input(&terminal_, buffer++, 1);
  }

  teken_pos_t final_cursor_pos = *teken_get_cursor(&terminal_);
  if (initial_cursor_pos.tp_row != final_cursor_pos.tp_row ||
      initial_cursor_pos.tp_col != final_cursor_pos.tp_col) {
    state_changes->cursor_changed = true;
    // Update dirty rect to include old and new cursor positions.
    current_state_changes_->dirty_rect = EnclosingRectangle(
        current_state_changes_->dirty_rect,
        Rectangle(initial_cursor_pos.tp_row, initial_cursor_pos.tp_col, 1, 1));
    current_state_changes_->dirty_rect = EnclosingRectangle(
        current_state_changes_->dirty_rect,
        Rectangle(final_cursor_pos.tp_row, final_cursor_pos.tp_col, 1, 1));
  }

  current_state_changes_ = nullptr;
}

MotermModel::Size MotermModel::GetSize() const {
  // Teken isn't const-correct, sadly.
  return TekenToMotermSize(
      *teken_get_winsize(const_cast<teken_t*>(&terminal_)));
}

MotermModel::Position MotermModel::GetCursorPosition() const {
  // Teken isn't const-correct, sadly.
  return TekenToMotermPosition(
      *teken_get_cursor(const_cast<teken_t*>(&terminal_)));
}

bool MotermModel::GetCursorVisibility() const {
  return cursor_visible_;
}

MotermModel::CharacterInfo MotermModel::GetCharacterInfoAt(
    const Position& position) const {
  FXL_DCHECK(position.row >= 0);
  FXL_DCHECK(position.row < static_cast<int>(GetSize().rows));
  FXL_DCHECK(position.column >= 0);
  FXL_DCHECK(position.column < static_cast<int>(GetSize().columns));

  uint32_t ch = characters_[position.row * size_.columns + position.column];
  const teken_attr_t& teken_attr =
      attributes_[position.row * size_.columns + position.column];
  Color fg = TekenToMotermColor(teken_attr.ta_fgcolor,
                                (teken_attr.ta_format & TF_BOLD));
  Color bg = TekenToMotermColor(teken_attr.ta_bgcolor, false);
  Attributes attr = 0;
  if ((teken_attr.ta_format & TF_BOLD))
    attr |= kAttributesBold;
  if ((teken_attr.ta_format & TF_UNDERLINE))
    attr |= kAttributesUnderline;
  if ((teken_attr.ta_format & TF_BLINK))
    attr |= kAttributesBlink;
  if ((teken_attr.ta_format & TF_REVERSE))
    std::swap(fg, bg);
  return CharacterInfo(ch, attr, fg, bg);
}

void MotermModel::SetSize(const Size& size, bool reset) {
  FXL_DCHECK(size.rows > 0u);
  FXL_DCHECK(size.columns > 0u);
  Size old_size = size_;
  size_ = size;
  size_t new_num_chars = size_.rows * size_.columns;
  if (num_chars_ != new_num_chars) {
    teken_char_t* new_characters = new teken_char_t[new_num_chars];
    memset(new_characters, 0, new_num_chars * sizeof(teken_char_t));

    teken_attr_t* new_attributes = new teken_attr_t[new_num_chars];
    memset(new_attributes, 0, new_num_chars * sizeof(teken_attr_t));

    // FIXME(jpoichet) copy from bottom to top instead
    for (size_t r = 0; r < old_size.rows; ++r) {
      size_t bytes = old_size.columns * sizeof(teken_char_t);
      if (r * size_.columns + bytes > new_num_chars) {
        // Until we copy bottom to top, ignore the last line.
        break;
      }
      memcpy(new_characters + r * size_.columns,
             characters_.get() + r * old_size.columns,
             old_size.columns * sizeof(teken_char_t));
      memcpy(new_attributes + r * size_.columns,
             attributes_.get() + r * old_size.columns,
             old_size.columns * sizeof(teken_attr_t));
    }
    characters_.reset(new_characters);
    attributes_.reset(new_attributes);
    num_chars_ = new_num_chars;
  }

  teken_pos_t teken_size = {static_cast<teken_unit_t>(size.rows),
                            static_cast<teken_unit_t>(size.columns)};
  if (reset) {
    teken_set_winsize_noreset(&terminal_, &teken_size);
  } else {
    // We'll try a bit harder to keep a sensible cursor position.
    teken_pos_t cursor_pos =
        *teken_get_cursor(const_cast<teken_t*>(&terminal_));
    teken_set_winsize(&terminal_, &teken_size);
    if (cursor_pos.tp_row >= teken_size.tp_row)
      cursor_pos.tp_row = teken_size.tp_row - 1;
    if (cursor_pos.tp_col >= teken_size.tp_col)
      cursor_pos.tp_col = teken_size.tp_col - 1;
    teken_set_cursor(&terminal_, &cursor_pos);
  }
}

void MotermModel::OnBell() {
  FXL_DCHECK(current_state_changes_);
  current_state_changes_->bell_count++;
}

void MotermModel::OnCursor(const teken_pos_t* pos) {
  FXL_DCHECK(current_state_changes_);
  // Don't do anything. We'll just compare initial and final cursor positions.
}

void MotermModel::OnPutchar(const teken_pos_t* pos,
                            teken_char_t ch,
                            const teken_attr_t* attr) {
  character_at(pos->tp_row, pos->tp_col) = ch;
  attribute_at(pos->tp_row, pos->tp_col) = *attr;

  // Update dirty rect.
  FXL_DCHECK(current_state_changes_);
  current_state_changes_->dirty_rect =
      EnclosingRectangle(current_state_changes_->dirty_rect,
                         Rectangle(pos->tp_row, pos->tp_col, 1, 1));
}

void MotermModel::OnFill(const teken_rect_t* rect,
                         teken_char_t ch,
                         const teken_attr_t* attr) {
  for (size_t row = rect->tr_begin.tp_row; row < rect->tr_end.tp_row; row++) {
    for (size_t col = rect->tr_begin.tp_col; col < rect->tr_end.tp_col; col++) {
      character_at(row, col) = ch;
      attribute_at(row, col) = *attr;
    }
  }

  // Update dirty rect.
  FXL_DCHECK(current_state_changes_);
  current_state_changes_->dirty_rect = EnclosingRectangle(
      current_state_changes_->dirty_rect, TekenToMotermRectangle(*rect));
}

void MotermModel::OnCopy(const teken_rect_t* rect, const teken_pos_t* pos) {
  unsigned height = rect->tr_end.tp_row - rect->tr_begin.tp_row;
  unsigned width = rect->tr_end.tp_col - rect->tr_begin.tp_col;

  // This is really a "move" (like |memmove()|) -- overlaps are likely. Process
  // the rows depending on which way (vertically) we're moving.
  if (pos->tp_row <= rect->tr_begin.tp_row) {
    // Start from the top row.
    for (unsigned row = 0; row < height; row++) {
      // Use |memmove()| here, to in case we're not moving vertically.
      memmove(&character_at(pos->tp_row + row, pos->tp_col),
              &character_at(rect->tr_begin.tp_row + row, pos->tp_col),
              width * sizeof(characters_[0]));
      memmove(&attribute_at(pos->tp_row + row, pos->tp_col),
              &attribute_at(rect->tr_begin.tp_row + row, pos->tp_col),
              width * sizeof(attributes_[0]));
    }
  } else {
    // Start from the bottom row.
    for (unsigned row = height; row > 0;) {
      row--;
      // We can use |memcpy()| here.
      memcpy(&character_at(pos->tp_row + row, pos->tp_col),
             &character_at(rect->tr_begin.tp_row + row, pos->tp_col),
             width * sizeof(characters_[0]));
      memcpy(&attribute_at(pos->tp_row + row, pos->tp_col),
             &attribute_at(rect->tr_begin.tp_row + row, pos->tp_col),
             width * sizeof(attributes_[0]));
    }
  }

  // Update dirty rect.
  FXL_DCHECK(current_state_changes_);
  current_state_changes_->dirty_rect = EnclosingRectangle(
      current_state_changes_->dirty_rect,
      Rectangle(static_cast<int>(pos->tp_row), static_cast<int>(pos->tp_col),
                width, height));
}

void MotermModel::OnParam(int cmd, unsigned val) {
  FXL_DCHECK(current_state_changes_);

  // Note: |val| is usually a "boolean", except for |TP_SETBELLPD| (for which
  // |val| can be decomposed using |TP_SETBELLPD_{PITCH, DURATION}()|).
  switch (cmd) {
    case TP_SHOWCURSOR:
      cursor_visible_ = !!val;
      current_state_changes_->cursor_changed = true;
      break;
    case TP_KEYPADAPP:
      if (delegate_)
        delegate_->OnSetKeypadMode(!!val);
      break;
    case TP_AUTOREPEAT:
    case TP_SWITCHVT:
    case TP_132COLS:
    case TP_SETBELLPD:
    case TP_MOUSE:
      // TODO(vtl): TP_KEYPADAPP seems especially common.
      FXL_NOTIMPLEMENTED() << "OnParam(" << cmd << ", " << val << ")";
      break;
    default:
      FXL_NOTREACHED() << "OnParam(): unknown command: " << cmd;
      break;
  }
}

void MotermModel::OnRespond(const void* buf, size_t size) {
  if (delegate_)
    delegate_->OnResponse(buf, size);
  else
    FXL_LOG(WARNING) << "Ignoring response: no delegate";
}

// static
void MotermModel::OnBellThunk(void* ctx) {
  FXL_DCHECK(ctx);
  return static_cast<MotermModel*>(ctx)->OnBell();
}

// static
void MotermModel::OnCursorThunk(void* ctx, const teken_pos_t* pos) {
  FXL_DCHECK(ctx);
  return static_cast<MotermModel*>(ctx)->OnCursor(pos);
}

// static
void MotermModel::OnPutcharThunk(void* ctx,
                                 const teken_pos_t* pos,
                                 teken_char_t ch,
                                 const teken_attr_t* attr) {
  FXL_DCHECK(ctx);
  return static_cast<MotermModel*>(ctx)->OnPutchar(pos, ch, attr);
}

// static
void MotermModel::OnFillThunk(void* ctx,
                              const teken_rect_t* rect,
                              teken_char_t ch,
                              const teken_attr_t* attr) {
  FXL_DCHECK(ctx);
  return static_cast<MotermModel*>(ctx)->OnFill(rect, ch, attr);
}

// static
void MotermModel::OnCopyThunk(void* ctx,
                              const teken_rect_t* rect,
                              const teken_pos_t* pos) {
  FXL_DCHECK(ctx);
  return static_cast<MotermModel*>(ctx)->OnCopy(rect, pos);
}

// static
void MotermModel::OnParamThunk(void* ctx, int cmd, unsigned val) {
  FXL_DCHECK(ctx);
  return static_cast<MotermModel*>(ctx)->OnParam(cmd, val);
}

// static
void MotermModel::OnRespondThunk(void* ctx, const void* buf, size_t size) {
  FXL_DCHECK(ctx);
  return static_cast<MotermModel*>(ctx)->OnRespond(buf, size);
}
