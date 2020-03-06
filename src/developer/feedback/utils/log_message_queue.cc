#include "src/developer/feedback/utils/log_message_queue.h"

#include <memory>
#include <mutex>

#include <trace/event.h>

namespace feedback {

using fuchsia::logger::LogMessage;

LogMessageQueue::LogMessageQueue(const size_t capacity) : capacity_(capacity) {}

void LogMessageQueue::Push(LogMessage log_message) {
  TRACE_DURATION("feedback:io", "LogMessageQueue::Push");

  std::lock_guard<std::mutex> lk(mtx_);

  // If the queue if full, drop the message
  if (messages_.size() == capacity_) {
    TRACE_INSTANT("feedback:io", "LogMessageQueue::Push::Drop", TRACE_SCOPE_PROCESS);
    return;
  }

  messages_.push_back(std::move(log_message));
  cv_.notify_all();
}

LogMessage LogMessageQueue::Pop() {
  TRACE_DURATION("feedback:io", "LogMessageQueue::Pop");

  std::unique_lock<std::mutex> lk(mtx_);

  // If there aren't any available messages, wait until a message is available.
  if (messages_.size() == 0) {
    // This releases the lock and re-acquires it when notified (here form notify_all() in Push()).
    TRACE_DURATION("feedback:io", "LogMessageQueue::Pop::Wait");
    cv_.wait(lk, [this] { return messages_.size() != 0; });
  }

  const LogMessage log_message = std::move(messages_.front());
  messages_.pop_front();

  return log_message;
}

}  // namespace feedback
