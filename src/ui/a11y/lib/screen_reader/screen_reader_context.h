// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SCREEN_READER_SCREEN_READER_CONTEXT_H_
#define SRC_UI_A11Y_LIB_SCREEN_READER_SCREEN_READER_CONTEXT_H_

#include <fuchsia/accessibility/tts/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/fit/function.h>

#include <memory>
#include <optional>

#include "src/ui/a11y/lib/screen_reader/focus/a11y_focus_manager.h"
#include "src/ui/a11y/lib/screen_reader/speaker.h"
#include "src/ui/a11y/lib/tts/tts_manager.h"
#include "src/ui/a11y/lib/view/view_source.h"

namespace a11y {

// ScreenReaderContext class stores the current state of the screen reader which includes the
// currently selected node(via the a11y focus manager) and state(currently selected semantic level).
// This class will be queried by "Actions" to get screen reader information.
class ScreenReaderContext {
 public:
  // Describes Screen Reader possible modes of navigation.
  enum class ScreenReaderMode {
    kNormal,  // Default case.
    // Whether a continuous exploration is in progress. A continuous exploration is a state
    // where an user is exploring the screen (by touch, for example), and is informed of the
    // elements in a11y focus (hearing the tts, for example). When in continuous exploration, if the
    // user stops at a particular semantic node, this node is informed only once, and another update
    // will only come after the user moves to a different node. In contrast, when the user is not in
    // continuous exploration, if the node is explored multiple times, they will always be informed.
    kContinuousExploration,
  };

  // Defines the different semantic levels.
  // A semantic level is a granularity level of navigation that is used to select the appropriate
  // action when the user performs actions of the form next / previous element. In order to select
  // what is the next element, the Screen Reader uses the semantic level to choose the appropriate
  // logic to use.
  enum class SemanticLevel {
    // Linear Navigation defines what will be the next / previous element.
    kDefault,
    // Adjusts a value in a slider or range control element.
    kAdjustValue,
    // User is navigating by characters of the text.
    kCharacter,
    // User is navigating by the words of the text.
    kWord,
    // User is navigating by the headings of the text.
    kHeader,
    // User is navigating by form controls.
    kFormControl,
  };

  struct TableContext {
    // The row/column headers, ordered by row/column index. Note that the
    // vectors are 0-indexed, so users must access the row/column header via the
    // row/column index - 1.
    std::vector<std::string> row_headers;
    std::vector<std::string> column_headers;

    // Row/column indices of the currently focused node.
    // Note that row and column indices are 1-indexed, so a value of 0
    // indicates that no row/column information is present.
    uint32_t row_index = 0;
    uint32_t column_index = 0;

    bool operator==(const TableContext& rhs) const {
      return this->row_headers == rhs.row_headers && this->column_headers == rhs.column_headers &&
             this->row_index == rhs.row_index && this->column_index == rhs.column_index;
    }
  };

  // TODO(fxb.dev/90733): Investigate whether we need to both a current and
  // previous copy of all this state.
  struct NavigationContext {
    // Holds koid of the view for the 'current node' (the node to which this context applies).
    // Note: It's possible for the screen reader to be in a degraded state where no node is in
    // focus, in which case this will be `nullopt`.
    std::optional<zx_koid_t> view_ref_koid;

    struct Container {
      uint32_t node_id;

      // If the container is a table, this holds additional info about the navigation state within
      // that table.
      std::optional<TableContext> table_context;

      bool operator==(const Container& rhs) const {
        return this->node_id == rhs.node_id && this->table_context == rhs.table_context;
      }
    };

    // Holds all containers that are ancestors of the current node. Sorted 'deepest-last'.
    // Will not include the current node itself.
    std::vector<Container> containers;
  };

  // Defines the signature for a callback invoked when a node update is
  // received.
  using OnNodeUpdateCallback = fit::function<void()>;

  // |a11y_focus_manager| will be owned by this class.
  // |tts_manager| is not kept, and must be valid only during this constructor, where
  // |tts_engine_ptr_| is instantiated.
  // |view_source| must outlive this object.
  explicit ScreenReaderContext(std::unique_ptr<A11yFocusManager> a11y_focus_manager,
                               TtsManager* tts_manager, ViewSource* view_source,
                               std::string locale_id = "en-US");

  virtual ~ScreenReaderContext();

  // Returns pointer to A11yFocusManager which stores a11y focus information for screen reader.
  virtual A11yFocusManager* GetA11yFocusManager();

  // Returns the Executor used by the Screen Reader to schedule promises.
  async::Executor* executor() { return &executor_; }

  virtual Speaker* speaker();

  // Sets the Screen Reader current mode.
  void set_mode(ScreenReaderMode mode) { mode_ = mode; }
  ScreenReaderMode mode() const { return mode_; }

  // Sets the Screen Reader semantic level.
  void set_semantic_level(SemanticLevel semantic_level) { semantic_level_ = semantic_level; }
  SemanticLevel semantic_level() const { return semantic_level_; }

  void set_locale_id(const std::string& locale_id) { locale_id_ = locale_id; }
  const std::string& locale_id() const { return locale_id_; }

  // Set/run on_node_update_callback_.
  void set_on_node_update_callback(OnNodeUpdateCallback callback) {
    on_node_update_callback_ = std::move(callback);
  }
  void run_and_clear_on_node_update_callback();
  bool has_on_node_update_callback() { return static_cast<bool>(on_node_update_callback_); }

  // Returns true if the node currently focused by the screen reader is a text field.
  virtual bool IsTextFieldFocused() const;

  // Returns true if the node currently focused by the screen reader is part of a virtual keyboard.
  virtual bool IsVirtualKeyboardFocused() const;

  // Tries to update the cache if the describable content of the a11y focused node has changed in
  // respect to the cached copy of the node. Returns true if the cache was updated. Please only
  // modify this function to add new describable content if the changes can be spoken. For example,
  // a change on the node location is not describable, because the screen reader does not report it
  // where a change in some attribute that is spoken to the user is.
  virtual bool UpdateCacheIfDescribableA11yFocusedNodeContentChanged();

  // Methods to get/set the current and previous navigation contexts.
  void set_current_navigation_context(NavigationContext navigation_context) {
    current_navigation_context_ = navigation_context;
  }
  void set_previous_navigation_context(NavigationContext navigation_context) {
    previous_navigation_context_ = navigation_context;
  }
  NavigationContext current_navigation_context() { return current_navigation_context_; }
  NavigationContext previous_navigation_context() { return previous_navigation_context_; }

  void set_last_interaction(zx::time last_interaction) { last_interaction_ = last_interaction; }
  zx::time last_interaction() const { return last_interaction_; }

 protected:
  // For mocks.
  ScreenReaderContext();

 private:
  // Helper method to retrieve a semantic node.
  const fuchsia::accessibility::semantics::Node* GetSemanticNode(zx_koid_t view_ref_koid,
                                                                 uint32_t node_id) const;

  async::Executor executor_;

  // Stores A11yFocusManager pointer.
  // A11yFocusManager pointer should never be nullptr.
  std::unique_ptr<A11yFocusManager> a11y_focus_manager_;

  // We need to keep a pointer to the TTS manager so that we can close the
  // engine we opened in the constructor.
  TtsManager* tts_manager_ = nullptr;

  // Interface used to obtain view data, including semantics.
  ViewSource* const view_source_ = nullptr;

  // Interface to the engine is owned by this class so that it can build and rebuild the Speaker
  // when the locale changes.
  fuchsia::accessibility::tts::EnginePtr tts_engine_ptr_;

  // Manages speech tasks of this screen reader.
  std::unique_ptr<Speaker> speaker_;

  // Current Screen Reader mode.
  ScreenReaderMode mode_ = ScreenReaderMode::kNormal;

  // Current semantic level.
  SemanticLevel semantic_level_ = SemanticLevel::kDefault;

  // Unicode BCP-47 Locale Identifier.
  std::string locale_id_;

  // Copy of the last node to receive the a11y focus.
  std::optional<fuchsia::accessibility::semantics::Node> last_a11y_focused_node_;

  // Holds state about the portions of the semantic tree surrounding the
  // currently focused node.
  NavigationContext current_navigation_context_ = {};
  NavigationContext previous_navigation_context_ = {};

  // Invoked once, on the first tree update received after the callback is set.
  // The callback is cleared after it's invoked.
  // Example use case: The ChangeRangeValueAction sets a callback to read the
  // updated slider value when the tree update setting the new value is
  // received.
  OnNodeUpdateCallback on_node_update_callback_ = {};

  // Saves the last time an user interacted with the device.
  zx::time last_interaction_;
};

class ScreenReaderContextFactory {
 public:
  ScreenReaderContextFactory() = default;
  virtual ~ScreenReaderContextFactory() = default;

  virtual std::unique_ptr<ScreenReaderContext> CreateScreenReaderContext(
      std::unique_ptr<A11yFocusManager> a11y_focus_manager, TtsManager* tts_manager,
      ViewSource* view_source, std::string locale_id = "en-US") {
    return std::make_unique<ScreenReaderContext>(std::move(a11y_focus_manager), tts_manager,
                                                 view_source, locale_id);
  }
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_SCREEN_READER_SCREEN_READER_CONTEXT_H_
