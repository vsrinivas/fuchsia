// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// |MotermModel| is a class providing a model for terminal emulation. The basic
// operations are providing "input" bytes (this is input from the point of view
// of the terminal; from the point of view of applications, it's output) and
// determining what character to display at any given position (with what
// attributes).
//
// Note that no termios-style processing of the "input" bytes is done. The
// "input" bytes should be as seen on the wire by a serial terminal.
//
// This class does not handle "output" from the terminal (i.e., its keyboard,
// and thus "input" to applications).
//
// The current implementation is on top of FreeBSD's libteken, though it would
// be straightforward to replace it with another terminal emulation library (or
// implement one directly).

#ifndef APPS_MOTERM_MOTERM_MODEL_H_
#define APPS_MOTERM_MOTERM_MODEL_H_

#include <stddef.h>
#include <stdint.h>

#include <memory>

#include "lib/fxl/macros.h"
#include "third_party/libteken/teken/teken.h"

class MotermModel {
 public:
  // Position: zero-based, starting from upper-left. (Quantities are signed to
  // allow for relative and off-screen positions.)
  struct Position {
    Position(int row = 0, int column = 0) : row(row), column(column) {}

    int row;
    int column;
  };

  struct Size {
    Size(unsigned rows = 0, unsigned columns = 0)
        : rows(rows), columns(columns) {}

    bool operator==(const Size& lhs) {
      return rows == lhs.rows && columns == lhs.columns;
    }

    unsigned rows;
    unsigned columns;
  };

  struct Rectangle {
    Rectangle(int row = 0,
              int column = 0,
              unsigned rows = 0,
              unsigned columns = 0)
        : position(row, column), size(rows, columns) {}

    bool IsEmpty() const { return !size.rows || !size.columns; }

    Position position;
    Size size;
  };

  struct Color {
    Color(uint8_t red = 0, uint8_t green = 0, uint8_t blue = 0)
        : red(red), green(green), blue(blue) {}

    uint8_t red;
    uint8_t green;
    uint8_t blue;
  };

  using Attributes = uint32_t;
  static const Attributes kAttributesBold = 1;
  static const Attributes kAttributesUnderline = 2;
  static const Attributes kAttributesBlink = 4;

  struct CharacterInfo {
    CharacterInfo(uint32_t code_point,
                  Attributes attributes,
                  const Color& foreground_color,
                  const Color& background_color)
        : code_point(code_point),
          attributes(attributes),
          foreground_color(foreground_color),
          background_color(background_color) {}

    uint32_t code_point;  // Unicode, of course.
    Attributes attributes;
    Color foreground_color;
    Color background_color;
  };

  struct StateChanges {
    StateChanges() : cursor_changed(false), bell_count(0), dirty_rect() {}

    bool IsDirty() const {
      return cursor_changed || bell_count > 0 || !dirty_rect.IsEmpty();
    }
    void Reset() { *this = StateChanges(); }

    bool cursor_changed;  // Moved or changed visibility.
    unsigned bell_count;
    Rectangle dirty_rect;
  };

  class Delegate {
   public:
    // Called when a response is received (i.e., the terminal wants to put data
    // into the input stream).
    virtual void OnResponse(const void* buf, size_t size) = 0;
    // Called for ESC > (false) and ESC = (true).
    virtual void OnSetKeypadMode(bool application_mode) = 0;

   protected:
    Delegate() {}
    virtual ~Delegate() {}

   private:
    FXL_DISALLOW_COPY_AND_ASSIGN(Delegate);
  };

  // If non-null, |delegate| must outlive this object.
  MotermModel(const Size& max_size, Delegate* delegate);
  ~MotermModel();

  // Process the given input bytes, reporting (additional) state changes to
  // |*state_changes| (note: this does not "reset" |*state_changes|, so that
  // state changes can be accumulated across multiple calls).
  void ProcessInput(const void* input_bytes,
                    size_t num_input_bytes,
                    StateChanges* state_changes);

  Size GetSize() const;
  Position GetCursorPosition() const;
  bool GetCursorVisibility() const;
  CharacterInfo GetCharacterInfoAt(const Position& position) const;

  void SetSize(const Size& size, bool reset);

 private:
  teken_char_t& character_at(unsigned row, unsigned column) {
    return characters_[row * size_.columns + column];
  }
  teken_attr_t& attribute_at(unsigned row, unsigned column) {
    return attributes_[row * size_.columns + column];
  }

  // libteken callbacks:
  void OnBell();
  void OnCursor(const teken_pos_t* pos);
  void OnPutchar(const teken_pos_t* pos,
                 teken_char_t ch,
                 const teken_attr_t* attr);
  void OnFill(const teken_rect_t* rect,
              teken_char_t ch,
              const teken_attr_t* attr);
  void OnCopy(const teken_rect_t* rect, const teken_pos_t* pos);
  void OnParam(int cmd, unsigned val);
  void OnRespond(const void* buf, size_t size);

  // Thunks for libteken callbacks:
  static void OnBellThunk(void* ctx);
  static void OnCursorThunk(void* ctx, const teken_pos_t* pos);
  static void OnPutcharThunk(void* ctx,
                             const teken_pos_t* pos,
                             teken_char_t ch,
                             const teken_attr_t* attr);
  static void OnFillThunk(void* ctx,
                          const teken_rect_t* rect,
                          teken_char_t ch,
                          const teken_attr_t* attr);
  static void OnCopyThunk(void* ctx,
                          const teken_rect_t* rect,
                          const teken_pos_t* pos);
  static void OnParamThunk(void* ctx, int cmd, unsigned val);
  static void OnRespondThunk(void* ctx, const void* buf, size_t size);

  Size size_;
  Delegate* const delegate_;
  size_t num_chars_;
  std::unique_ptr<teken_char_t[]> characters_;
  std::unique_ptr<teken_attr_t[]> attributes_;
  bool cursor_visible_;

  teken_t terminal_;

  // Used by the callbacks. ("Usually" null, but must be non-null whenever a
  // callback may be called -- it'll point to a stack variable.)
  StateChanges* current_state_changes_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MotermModel);
};

#endif  // APPS_MOTERM_MOTERM_MODEL_H_
