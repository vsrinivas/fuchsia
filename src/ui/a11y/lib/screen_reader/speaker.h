// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SCREEN_READER_SPEAKER_H_
#define SRC_UI_A11Y_LIB_SCREEN_READER_SPEAKER_H_

#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include <fuchsia/accessibility/tts/cpp/fidl.h>
#include <lib/fit/bridge.h>
#include <lib/fit/promise.h>
#include <lib/zx/time.h>

#include <memory>
#include <queue>

#include "src/ui/a11y/lib/screen_reader/screen_reader_message_generator.h"

namespace a11y {

// A Speaker manages speech tasks to be executed by the Screen Reader.
//
// Speech tasks are represented in the form of fit::promises. A task manages the dispatch of
// utterances, in the right order and at the right time,  that together make a node description.
// Please see ScreenReaderMessageGenerator for more details. Speech tasks must run at the same
// executor. A task can wait on another task to finish before it starts or start right away,
// depending on the option selected. Please see Options for details. A task is not added to the
// queue of tasks until it runs. This allows creating multiple speech tasks in any order, but
// controlling the order they will run at dispatch time, not at building time. Important! The
// description of a node is built at task creation time, not during run time. this simplifies the
// management of semantic nodes and their life time. This guarantees that no reference to a semantic
// node is kept inside of the task, creating the problem of keeping a node alive until the task
// finishes running.
class Speaker {
 public:
  // Options that controls how a task will run.
  struct Options {
    // If true, this task will interrupt any playing tts and cancels pending utterances to be
    // spoken. It starts right away.
    bool interrupt = true;
    // Delay before utterance is vocalized.
    zx::duration delay = zx::msec(0);
    // Whether the utterance of the task is saved for later inspection.
    bool save_utterance = true;
  };

  explicit Speaker(fuchsia::accessibility::tts::EnginePtr* tts_engine_ptr,
                   std::unique_ptr<ScreenReaderMessageGenerator> screen_reader_message_generator);
  virtual ~Speaker();

  // Returns a speech task that speaks the node description.
  virtual fit::promise<> SpeakNodePromise(const fuchsia::accessibility::semantics::Node* node,
                                          Options options);

  // Returns a speech task that speaks the provided |message|.
  virtual fit::promise<> SpeakMessagePromise(fuchsia::accessibility::tts::Utterance utterance,
                                             Options options);

  // Returns a speech task that speaks the canonical message specified by
  // |message_id|.
  virtual fit::promise<> SpeakMessageByIdPromise(fuchsia::intl::l10n::MessageIds message_id,
                                                 Options options);

  // Returns a promise that cancels pending or in progress tts utterances.
  virtual fit::promise<> CancelTts();

  // Returns a string with the last spoken utterance.
  virtual const std::string& last_utterance() const { return last_utterance_; }

  // Sets a message to be spoken just before this object is destroyed.
  virtual void set_epitaph(fuchsia::intl::l10n::MessageIds epitaph) { epitaph_ = epitaph; }

 protected:
  // For mocks.
  Speaker() = default;

 private:
  // A speech task holds the data needed to speak a description. The object is passed to the several
  // async blocks of code (promises), that run in some determined sequence to speak the utterances
  // of the description, in the correct order and at the right time. If a task goes out of scope,
  // this implies that it has been canceled, meaning that the async blocks that may consume it must
  // always check for its validity before accessing. The owner of a SpeechTask, normally a queue of
  // tasks, constructs them in a std::shared_ptr, while async code receive a std::weak_ptr, which
  // must be locked before accessing, to guarantee the existence of the task.
  struct SpeechTask {
    SpeechTask(std::vector<ScreenReaderMessageGenerator::UtteranceAndContext> utterances_arg);
    ~SpeechTask();
    std::vector<ScreenReaderMessageGenerator::UtteranceAndContext> utterances;
    // The current utterance in |utterances| being spoken.
    int utterance_index = 0;
    // Invoked when this task is at the front of the queue and can be executed.
    fit::completer<> starter;
  };

  // Prepares the task for execution. If interrupting or at the front of the queue, starts right
  // away, waits  for its turn otherwise.
  fit::promise<> PrepareTask(std::shared_ptr<SpeechTask> task, bool interrupt, bool save_utterance);

  // Dispatches all utterances of this task to be spoken, respecting their order and time spacing
  // requirements.
  fit::promise<> DispatchUtterances(std::shared_ptr<SpeechTask> task, bool interrupt);

  // Dispatches a single utterance to the tts engine.
  fit::promise<> DispatchSingleUtterance(std::weak_ptr<SpeechTask> weak_task);

  // Ends this speech task, removing it from the queue. If the queue is not empty after removal,
  // also informs the new front of the queue task that it can start running.
  fit::promise<> EndSpeechTask(std::weak_ptr<SpeechTask> weak_task, bool success);

  // The task waits in queue until it reaches the front of the queue.
  fit::promise<> WaitInQueue(std::weak_ptr<SpeechTask> weak_task);

  // Returns a promise that enqueues an utterance. An error is thrown if the atempt to enqueue the
  // utterance is rejected by the TTS service.
  fit::promise<> EnqueueUtterance(fuchsia::accessibility::tts::Utterance utterance);

  // Returns a promise that speaks enqueued utterances. An error is thrown if the atempt to speak
  // the utterance(s) is rejected by the TTS service.
  fit::promise<> Speak();

  // Interface to the tts service that receives utterance requests.
  fuchsia::accessibility::tts::EnginePtr* tts_engine_ptr_ = nullptr;

  // Used to generate node descriptions.
  std::unique_ptr<ScreenReaderMessageGenerator> screen_reader_message_generator_;

  // queue of speech tasks. Only the front of the queue is running, while others wait for it to
  // finish.
  std::queue<std::shared_ptr<SpeechTask>> queue_;

  // The last spoken utterance.
  std::string last_utterance_;

  // If set, contains a message to be spoken just before this object is destroyed.
  std::optional<fuchsia::intl::l10n::MessageIds> epitaph_;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_SCREEN_READER_SPEAKER_H_
