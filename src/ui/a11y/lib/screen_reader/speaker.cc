// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/speaker.h"

#include <lib/async/default.h>
#include <lib/fit/bridge.h>
#include <lib/syslog/cpp/macros.h>

#include <chrono>
#include <thread>

namespace a11y {
namespace {

using fuchsia::accessibility::tts::Utterance;

// Delays the task by |delay| msec, resuming its execution after.
void DelayTask(int64_t delay, fit::suspended_task task) {
  std::thread([delay, task]() mutable {
    std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    task.resume_task();
  }).detach();
}

// Concatenates all utterance strings into a single string.
std::string ConcatenateUtterances(
    const std::vector<ScreenReaderMessageGenerator::UtteranceAndContext>& utterances) {
  std::string utterance;
  if (!utterances.empty()) {
    auto it = utterances.begin();
    utterance = it->utterance.has_message() ? it->utterance.message() : "";
    it++;
    while (it != utterances.end()) {
      if (it->utterance.has_message()) {
        utterance += " " + it->utterance.message();
      }
      it++;
    }
  }
  return utterance;
}

}  // namespace

Speaker::Speaker(fuchsia::accessibility::tts::EnginePtr* tts_engine_ptr,
                 std::unique_ptr<ScreenReaderMessageGenerator> screen_reader_message_generator)
    : tts_engine_ptr_(tts_engine_ptr),
      screen_reader_message_generator_(std::move(screen_reader_message_generator)) {
  FX_DCHECK(tts_engine_ptr_);
}

Speaker::~Speaker() {
  if (epitaph_) {
    // This logic here is necessary in order to provide a clean way for the Screen Reader to
    // announce that it is turning off. Because this class generates promises that reference itself,
    // and those promises run on a loop that runs after this object has been destroyed, we need a
    // direct way of making a last message to be spoken.
    auto utterance = screen_reader_message_generator_->GenerateUtteranceByMessageId(*epitaph_);
    // There is no time to check back the results, so makes a best effort to speak whatever is here
    // before sutting down.
    (*tts_engine_ptr_)->Enqueue(std::move(utterance.utterance), [](auto...) {});
    (*tts_engine_ptr_)->Speak([](auto...) {});
  }
}

fit::promise<> Speaker::SpeakNodePromise(const fuchsia::accessibility::semantics::Node* node,
                                         Options options) {
  auto utterances = screen_reader_message_generator_->DescribeNode(node);
  auto task = std::make_shared<SpeechTask>(std::move(utterances));

  return PrepareTask(task, options.interrupt, options.save_utterance)
      .and_then(DispatchUtterances(task, options.interrupt));
}

fit::promise<> Speaker::SpeakMessagePromise(Utterance utterance, Options options) {
  std::vector<ScreenReaderMessageGenerator::UtteranceAndContext> utterances;
  ScreenReaderMessageGenerator::UtteranceAndContext utterance_and_context;
  utterance_and_context.utterance = std::move(utterance);
  utterances.push_back(std::move(utterance_and_context));
  auto task = std::make_shared<SpeechTask>(std::move(utterances));

  return PrepareTask(task, options.interrupt, options.save_utterance)
      .and_then(DispatchUtterances(task, options.interrupt));
}

fit::promise<> Speaker::SpeakMessageByIdPromise(fuchsia::intl::l10n::MessageIds message_id,
                                                Options options) {
  std::vector<ScreenReaderMessageGenerator::UtteranceAndContext> utterances;
  utterances.emplace_back(
      screen_reader_message_generator_->GenerateUtteranceByMessageId(message_id));
  FX_DCHECK(!utterances.empty());
  auto task = std::make_shared<SpeechTask>(std::move(utterances));

  return PrepareTask(task, options.interrupt, options.save_utterance)
      .and_then(DispatchUtterances(task, options.interrupt));
}

fit::promise<> Speaker::PrepareTask(std::shared_ptr<SpeechTask> task, bool interrupt,
                                    bool save_utterance) {
  return fit::make_promise([this, task, interrupt, save_utterance]() mutable -> fit::promise<> {
    if (save_utterance) {
      last_utterance_ = ConcatenateUtterances(task->utterances);
    }
    if (interrupt) {
      decltype(queue_) empty;
      std::swap(empty, queue_);
      queue_.push(std::move(task));
      // This task trumps whatever is speaking and starts now, so it cancels any pending task.
      return CancelTts();
    }
    // Even when not interrupting, the task needs to be part of the queue.
    queue_.push(std::move(task));
    if (queue_.size() == 1) {
      // This is the only task in the queue, it can start right away.
      return fit::make_ok_promise();
    }
    return WaitInQueue(std::weak_ptr(queue_.back()));
  });
}

fit::promise<> Speaker::DispatchUtterances(std::shared_ptr<SpeechTask> task, bool interrupt) {
  return fit::make_promise([this, weak_task = std::weak_ptr(task)]() mutable -> fit::promise<> {
           return DispatchSingleUtterance(std::move(weak_task));
         })
      .and_then([this, weak_task = std::weak_ptr(task), interrupt]() mutable -> fit::promise<> {
        auto task = weak_task.lock();
        if (!task) {
          return EndSpeechTask(std::move(weak_task), /*success=*/false);
        }
        if (static_cast<uint64_t>(task->utterance_index) < task->utterances.size()) {
          return DispatchUtterances(std::move(task), interrupt);
        }
        return EndSpeechTask(std::move(weak_task), /*success=*/true);
      });
}

fit::promise<> Speaker::DispatchSingleUtterance(std::weak_ptr<SpeechTask> weak_task) {
  auto task = weak_task.lock();
  if (!task) {
    return EndSpeechTask(std::move(weak_task), /*success=*/false);
  }

  return fit::make_promise([delay_elapsed = false, weak_task = std::weak_ptr(task)](
                               fit::context& context) mutable -> fit::result<Utterance> {
           auto task = weak_task.lock();
           if (!task) {
             return fit::error();
           }
           FX_DCHECK(static_cast<uint64_t>(task->utterance_index) < task->utterances.size());
           auto& utterance_and_context = task->utterances[task->utterance_index];
           if (utterance_and_context.delay.to_msecs() > 0 && !delay_elapsed) {
             // DelayTask() will have the capability of resuming this task. The task will run again,
             // but delay_elapsed will be true, which means that the task continues its normal
             // execution.
             DelayTask(utterance_and_context.delay.to_msecs(), context.suspend_task());
             delay_elapsed = true;
             return fit::pending();
           }
           task->utterance_index++;
           return fit::ok(std::move(utterance_and_context.utterance));
         })
      .and_then([this](Utterance& utterance) mutable -> fit::promise<> {
        return EnqueueUtterance(std::move(utterance));
      })
      .or_else([this, weak_task = std::weak_ptr(task)]() mutable {
        return EndSpeechTask(std::move(weak_task), /*success=*/false);
      })
      .and_then([this, weak_task = std::weak_ptr(task)]() mutable -> fit::promise<> {
        auto task = weak_task.lock();
        if (!task) {
          return EndSpeechTask(std::move(weak_task), /*success=*/false);
        }
        return Speak();
      });
}

fit::promise<> Speaker::CancelTts() {
  fit::bridge<> bridge;
  (*tts_engine_ptr_)->Cancel([completer = std::move(bridge.completer)]() mutable {
    completer.complete_ok();
  });
  return bridge.consumer.promise_or(fit::error());
}

fit::promise<> Speaker::EndSpeechTask(std::weak_ptr<SpeechTask> weak_task, bool success) {
  // If the task no longer exists, this means that it has already been deleted by another task.
  auto task = weak_task.lock();
  if (!task) {
    return fit::make_error_promise();
  }
  // Remove the task from the queue.
  queue_.pop();
  // Informs the new first task of the queue that it can start running.
  if (!queue_.empty()) {
    queue_.front()->starter.complete_ok();
  }
  if (!success) {
    return fit::make_error_promise();
  }

  return fit::make_ok_promise();
}

fit::promise<> Speaker::EnqueueUtterance(Utterance utterance) {
  fit::bridge<> bridge;
  (*tts_engine_ptr_)
      ->Enqueue(std::move(utterance),
                [completer = std::move(bridge.completer)](
                    fuchsia::accessibility::tts::Engine_Enqueue_Result result) mutable {
                  if (result.is_err()) {
                    FX_LOGS(ERROR) << "Error returned while calling tts::Enqueue().";
                    return completer.complete_error();
                  }
                  completer.complete_ok();
                });
  return bridge.consumer.promise_or(fit::error());
}

fit::promise<> Speaker::Speak() {
  fit::bridge<> bridge;
  (*tts_engine_ptr_)
      ->Speak([completer = std::move(bridge.completer)](
                  fuchsia::accessibility::tts::Engine_Speak_Result result) mutable {
        if (result.is_err()) {
          FX_LOGS(ERROR) << "Speaker: Error returned while calling tts::Speak().";
          return completer.complete_error();
        }
        completer.complete_ok();
      });
  return bridge.consumer.promise_or(fit::error());
}

fit::promise<> Speaker::WaitInQueue(std::weak_ptr<SpeechTask> weak_task) {
  auto task = weak_task.lock();
  if (!task) {
    return EndSpeechTask(std::move(weak_task), /*success=*/false);
  }
  fit::bridge<> bridge;
  // This completer will be invoked once this task reaches the front of the queue of tasks, ending
  // the wait.
  task->starter = std::move(bridge.completer);
  return bridge.consumer.promise_or(fit::error());
}

Speaker::SpeechTask::SpeechTask(
    std::vector<ScreenReaderMessageGenerator::UtteranceAndContext> utterances_arg)
    : utterances(std::move(utterances_arg)) {}

Speaker::SpeechTask::~SpeechTask() {
  if (starter) {
    // This fit::completer still holds the capability of completing this task, so it needs to inform
    // any promise that is waiting on it that it has been abandoned.
    starter.abandon();
  }
}

}  // namespace a11y
