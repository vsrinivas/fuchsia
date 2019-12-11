// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_LINE_INPUT_MODAL_LINE_INPUT_H_
#define SRC_LIB_LINE_INPUT_MODAL_LINE_INPUT_H_

#include "lib/fit/function.h"
#include "src/lib/line_input/line_input.h"

namespace line_input {

struct ModalPromptOptions {
  // When set, requires that the user press enter after typing. Otherwise, if the user has typed
  // an input that matches one of the options it will be implicitly accepted. Implicit enter
  // normally only makes sense for single-letter input ("y"/"n" type things).
  bool require_enter = true;

  // Compares a lower-case version of the user input to the option values. The option values must
  // be lower-case for this to work. The lower-cased version of the input will be passed to the
  // accept callback.
  bool case_sensitive = false;

  // Possible valid options that will cause the prompt to accept the input. If accepting case
  // insensitive input, these should be lower-case.
  std::vector<std::string> options;

  // When nonempty, this string input will be sent when control-C is pressed. This provides a way
  // for the caller to specify the behavior of Control-C without having another code path.
  //
  // This should be one of the |options| strings. It will be passed unvalidated to the callback.
  std::string cancel_option;
};

// Manages multiple line input objects to manage regular input and temporary modal input for
// questions. This is a base class, it delegates to a derived class to provide the line editor
// implementations for different I/O schemes.
class ModalLineInput : public LineInput {
 public:
  // In response to this callback, the implementation should call EndModal() if modal input is
  // complete.
  using ModalCompletionCallback = fit::function<void(const std::string&)>;

  // Callback that the modal input is about to be shown. In the normal case where there is no
  // current modal prompt open, it will be called immediately from BeginModal(). But implementing
  // this allows the embedder to properly handle the nested modal prompt case.
  //
  // It is expected that embedders will use this to display text that would go above the modal
  // prompt.
  using WillShowModalCallback = fit::callback<void()>;

  // Must call Init() before using any functions.
  explicit ModalLineInput() = default;
  virtual ~ModalLineInput() = default;

  // This can't be in the constructor because it needs to call virtual functions.
  void Init(AcceptCallback accept_cb, const std::string& prompt);

  // LineInput implementation.
  void SetAutocompleteCallback(AutocompleteCallback cb) override;
  void SetChangeCallback(ChangeCallback cb) override;
  void SetCancelCallback(CancelCallback cb) override;
  void SetEofCallback(EofCallback cb) override;
  void SetMaxCols(size_t max) override;
  const std::string& GetLine() const override;
  const std::deque<std::string>& GetHistory() const override;
  void OnInput(char c) override;
  void AddToHistory(const std::string& line) override;
  void Hide() override;
  void Show() override;

  // Higher-level version of BeginModal() and EndModal() that takes a list of possible options and
  // will call the callback only when the user enters a match for one of the options. The completion
  // callback should not need to call EndModal(), it will be done automatically when a valid input
  // is selected.
  void ModalGetOption(const ModalPromptOptions& options, const std::string& prompt,
                      ModalCompletionCallback cb,
                      WillShowModalCallback will_show = WillShowModalCallback());

  // Begins a modal question with the given prompt. The normal prompt will be hidden and replaced
  // with the given one. The callback (see definition above for implementation requirements) will be
  // called when the user presses enter. This callback should call EndModal() if the input is
  // accepted.
  //
  // There can be multiple callbacks happening at the same time. If there is a current modal input
  // active at the time of this call, the new one will be added to a queue and will be shown when
  // when the modal prompts before it have been completed.
  //
  // The "will show" callback may be called from within this function (see its definition above).
  void BeginModal(const std::string& prompt, ModalCompletionCallback cb,
                  WillShowModalCallback will_show = WillShowModalCallback());

  // Closes the current modal entry. If there is another modal prompt in the queue, it will be
  // shown. If there is none, the normal prompt will be shown again.
  //
  // Normally this will be called from within the completion callback of BeginModal() when the
  // input is accepted.
  void EndModal();

 private:
  // Factory function for the LineInput variant used by this class.
  virtual std::unique_ptr<LineInput> MakeLineInput(AcceptCallback accept_cb,
                                                   const std::string& prompt) = 0;

 private:
  // Called when there is a modal dialog to show at the front of modal_callbacks_.
  //
  // The normal input should be hidden before this call.
  void ShowNextModal();

  std::unique_ptr<LineInput> MakeAndSetupLineInput(AcceptCallback accept_cb,
                                                   const std::string& prompt);

  std::unique_ptr<LineInput> normal_input_;
  std::unique_ptr<LineInput> modal_input_;

  // Old modal input that should be deleted in the next call to OnInput(). This allows us to avoid
  // deleting it from withinn its own call stack (normally EndModal() is called from within its
  // accept callback).
  std::unique_ptr<LineInput> to_delete_;

  LineInput* current_input_ = nullptr;  // Points to either the normal or modal input.

  int max_cols_ = 0;

  bool hidden_ = true;

  EofCallback eof_callback_;

  // Will be nonempty when a modal question is being asked. front() is the current callback, with
  // later requests going toward the back().
  struct ModalRecord {
    std::string prompt;
    ModalCompletionCallback complete;
    WillShowModalCallback will_show;
  };
  std::deque<ModalRecord> modal_callbacks_;
};

class ModalLineInputStdout : public ModalLineInput {
 public:
  ModalLineInputStdout() : ModalLineInput() {}
  ~ModalLineInputStdout() override {}

 protected:
  std::unique_ptr<LineInput> MakeLineInput(AcceptCallback accept_cb,
                                           const std::string& prompt) override {
    return std::make_unique<LineInputStdout>(std::move(accept_cb), prompt);
  }
};

}  // namespace line_input

#endif  // SRC_LIB_LINE_INPUT_MODAL_LINE_INPUT_H_
