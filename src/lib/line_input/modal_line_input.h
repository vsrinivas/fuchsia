// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_LINE_INPUT_MODAL_LINE_INPUT_H_
#define SRC_LIB_LINE_INPUT_MODAL_LINE_INPUT_H_

#include "lib/fit/function.h"
#include "src/lib/line_input/line_input.h"

namespace line_input {

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
  void SetEofCallback(EofCallback cb) override;
  void SetMaxCols(size_t max) override;
  const std::string& GetLine() const override;
  const std::deque<std::string>& GetHistory() const override;
  void OnInput(char c) override;
  void AddToHistory(const std::string& line) override;
  void Hide() override;
  void Show() override;

  // Begins a modal question with the given prompt. The normal prompt will be hidden and replaced
  // with the given one. The callback (see definition above for implementation requirements) will be
  // called when the user presses enter.
  //
  // There are be multiple callbacks happeening at the same time. If the is a current modal input
  // active at the time of this call, the new one will be added to a queue and will be shown when
  // when the modal prompts before it have been completed.
  //
  // The "will show" callback may be called from within this function (see its definition above).
  void BeginModal(const std::string& prompt, ModalCompletionCallback cb,
                  WillShowModalCallback will_show = WillShowModalCallback());

  // Closes the current modal entry. If there is another modal prompt in the queue, it will be
  // shown. If there is none, the normal prompt will be shown again.
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
