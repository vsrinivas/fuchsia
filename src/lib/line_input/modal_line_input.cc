// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/line_input/modal_line_input.h"

#include <locale>

#include "src/lib/fxl/logging.h"

namespace line_input {

namespace {

// State associated with running a modal options prompt.
class ModalOptionState {
 public:
  ModalOptionState(ModalLineInput* input, ModalPromptOptions opts,
                   ModalLineInput::ModalCompletionCallback cb)
      : input_(input), options_(std::move(opts)), on_complete_(std::move(cb)) {}

  void OnAccept(const std::string& line) { CheckAccept(line, true); }

  void OnChanged(const std::string& line) {
    if (options_.require_enter)
      return;  // Nothing to do.

    if (CheckAccept(line, false)) {
      // When the user has typed valid input and we don't require enter, synthesize an enter to
      // invoke the normal accept codepath. We could close the input now, but skipping tne
      // enter will erase the current line in normal console mode.
      //
      // This will cause OnAccept() above to be called which will then signal completion.
      input_->OnInput('\r');
    }
  }

 private:
  // Checks whether the current line is a valid option. On success, returns true and optionally
  // signals completion (which closes the modal prompt).
  bool CheckAccept(const std::string& line, bool signal_complete) {
    // Optionally check case-insensitively.
    std::string to_check(line);
    if (!options_.case_sensitive) {
      for (size_t i = 0; i < to_check.size(); i++)
        to_check[i] = std::tolower(to_check[i]);
    }

    for (const auto& opt : options_.options) {
      if (to_check == opt) {
        if (signal_complete) {
          input_->EndModal();
          on_complete_(to_check);
        }
        return true;
      }
    }

    return false;
  }

  ModalLineInput* input_;  // Non-owning (it owns us).
  ModalPromptOptions options_;
  ModalLineInput::ModalCompletionCallback on_complete_;
};

}  // namespace

void ModalLineInput::Init(AcceptCallback accept_cb, const std::string& prompt) {
  FXL_DCHECK(!normal_input_) << "Calling Init() twice.";

  normal_input_ = MakeAndSetupLineInput(std::move(accept_cb), prompt);
  current_input_ = normal_input_.get();
}

void ModalLineInput::SetAutocompleteCallback(AutocompleteCallback cb) {
  FXL_DCHECK(normal_input_) << "Need to call Init() first.";
  // Autocomplete only works for the non-modal input.
  normal_input_->SetAutocompleteCallback(std::move(cb));
}

void ModalLineInput::SetChangeCallback(ChangeCallback cb) {
  // Change callbacks only go to the non-modal input. Our model interface handles changes on the
  // modal one.
  normal_input_->SetChangeCallback(std::move(cb));
}

void ModalLineInput::SetEofCallback(EofCallback cb) { eof_callback_ = std::move(cb); }

void ModalLineInput::SetMaxCols(size_t max) {
  FXL_DCHECK(normal_input_) << "Need to call Init() first.";

  max_cols_ = max;
  normal_input_->SetMaxCols(max);
  if (modal_input_)
    modal_input_->SetMaxCols(max);
}

const std::string& ModalLineInput::GetLine() const { return current_input_->GetLine(); }

const std::deque<std::string>& ModalLineInput::GetHistory() const {
  // History always comes from the regular one. The modal input has no history.
  FXL_DCHECK(normal_input_) << "Need to call Init() first.";
  return normal_input_->GetHistory();
}

void ModalLineInput::OnInput(char c) {
  if (to_delete_)
    to_delete_.reset();
  current_input_->OnInput(c);
}

void ModalLineInput::AddToHistory(const std::string& line) {
  // History always goes to the normal input.
  normal_input_->AddToHistory(line);
}

void ModalLineInput::Hide() {
  hidden_ = true;
  current_input_->Hide();
}

void ModalLineInput::Show() {
  hidden_ = false;
  current_input_->Show();
}

void ModalLineInput::ModalGetOption(const ModalPromptOptions& options, const std::string& prompt,
                                    ModalCompletionCallback cb, WillShowModalCallback will_show) {
  auto state = std::make_shared<ModalOptionState>(this, options, std::move(cb));

  // This will show callback registers our changed callback on the new modal input class and
  // calls the user will_show callback if provided. |this| can be captured here because this
  // owns the callback.
  auto do_will_show = [this, state, will_show = std::move(will_show)]() mutable {
    modal_input_->SetChangeCallback([state](const std::string& line) { state->OnChanged(line); });
    if (will_show)
      will_show();
  };

  BeginModal(
      prompt, [state](const std::string& line) { state->OnAccept(line); }, std::move(do_will_show));
}

void ModalLineInput::BeginModal(const std::string& prompt, ModalCompletionCallback cb,
                                WillShowModalCallback will_show) {
  auto& record = modal_callbacks_.emplace_back();
  record.prompt = prompt;
  record.complete = std::move(cb);
  record.will_show = std::move(will_show);

  if (!modal_input_) {
    // Not showing a modal input already, switch to it.
    if (!hidden_)
      normal_input_->Hide();
    ShowNextModal();
  }
  // Otherwise we're already showing a modal input. This new one will be automatically shown in
  // time.
}

void ModalLineInput::EndModal() {
  FXL_DCHECK(modal_input_) << "Not in a modal input.";
  FXL_DCHECK(!modal_callbacks_.empty());

  modal_callbacks_.pop_front();

  if (!hidden_)
    modal_input_->Hide();

  // Schedule the modal input to be deleted in next OnInput() call to prevent reentrancy.
  FXL_DCHECK(!to_delete_);
  to_delete_ = std::move(modal_input_);

  current_input_ = normal_input_.get();

  if (modal_callbacks_.empty()) {
    // Go back to normal mode.
    if (!hidden_)
      normal_input_->Show();
  } else {
    ShowNextModal();
  }
}

void ModalLineInput::ShowNextModal() {
  FXL_DCHECK(!modal_callbacks_.empty());
  FXL_DCHECK(!modal_input_);

  auto& record = modal_callbacks_.front();

  modal_input_ = MakeAndSetupLineInput(std::move(record.complete), record.prompt);
  current_input_ = modal_input_.get();

  if (record.will_show)
    record.will_show();

  if (!hidden_)
    current_input_->Show();
}

std::unique_ptr<LineInput> ModalLineInput::MakeAndSetupLineInput(AcceptCallback accept_cb,
                                                                 const std::string& prompt) {
  auto input = MakeLineInput(std::move(accept_cb), prompt);

  if (max_cols_)
    input->SetMaxCols(max_cols_);

  input->SetEofCallback([this]() {
    if (eof_callback_)
      eof_callback_();
  });

  return input;
}

}  // namespace line_input
