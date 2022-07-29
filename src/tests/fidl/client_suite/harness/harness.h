// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fidl.clientsuite/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

#include "src/tests/fidl/channel_util/channel.h"

#ifndef CLIENT_SUITE_HARNESS
#define CLIENT_SUITE_HARNESS

namespace client_suite::internal {

class Finisher : public fidl::Server<fidl_clientsuite::Finisher> {
 public:
  void Finish(FinishRequest& request, FinishCompleter::Sync& completer) override;

  void AddError(const std::string& str) { errors_.push_back(str); }

 private:
  std::vector<std::string> errors_;
  bool finished_ = false;
};

template <uint32_t Test>
class TestHandler {
 public:
  TestHandler(channel_util::Channel channel, std::shared_ptr<Finisher> finisher)
      : channel_(std::move(channel)), finisher_(std::move(finisher)) {}
  void Invoke() {
    FX_LOGS(INFO) << "Running " << TestName();
    RunHandler();
  }

  Finisher& finisher() { return *finisher_; }
  const Finisher& finisher() const { return *finisher_; }

 protected:
  channel_util::Channel& channel() { return channel_; }
  const channel_util::Channel& channel() const { return channel_; }

 private:
  void RunHandler();
  std::string_view TestName();

  channel_util::Channel channel_;
  std::shared_ptr<Finisher> finisher_;
};

using TestHandlerFunc = std::function<void(channel_util::Channel, std::shared_ptr<Finisher>)>;
bool RegisterTestHandler(uint32_t key, TestHandlerFunc value);
TestHandlerFunc LookupTestHandler(fidl_clientsuite::Test);

void ReportVerificationFailure(Finisher& finisher, std::string_view file, int line,
                               std::string_view cond, std::string_view message);

}  // namespace client_suite::internal

#define TEST_HANDLER(test)                                                               \
  [[maybe_unused]] static bool __handler_reg_##test =                                    \
      ::client_suite::internal::RegisterTestHandler(                                     \
          fidl_clientsuite::Test::k##test,                                               \
          [](channel_util::Channel channel,                                              \
             std::shared_ptr<client_suite::internal::Finisher> finisher) {               \
            return client_suite::internal::TestHandler<fidl_clientsuite::Test::k##test>( \
                       std::move(channel), std::move(finisher))                          \
                .Invoke();                                                               \
          });                                                                            \
  template <>                                                                            \
  std::string_view                                                                       \
  client_suite::internal::TestHandler<fidl_clientsuite::Test::k##test>::TestName() {     \
    return #test;                                                                        \
  }                                                                                      \
  template <>                                                                            \
  void client_suite::internal::TestHandler<fidl_clientsuite::Test::k##test>::RunHandler()

#define VERIFY_TRUE_MSG(cond, msg)                                                             \
  {                                                                                            \
    if (!(cond)) {                                                                             \
      client_suite::internal::ReportVerificationFailure(finisher(), __FILE__, __LINE__, #cond, \
                                                        msg);                                  \
      return;                                                                                  \
    }                                                                                          \
  }
#define VERIFY_FALSE_MSG(msg) VERIFY_TRUE_MSG(!(cond), msg)
#define VERIFY_EQ_MSG(cond1, cond2, msg) VERIFY_TRUE_MSG((cond1) == (cond2), msg)
#define VERIFY_NE_MSG(cond1, cond2, msg) VERIFY_TRUE_MSG((cond1) != (cond2), msg)
#define VERIFY_OK_MSG(cond, msg) VERIFY_EQ_MSG(ZX_OK, cond, msg)

#define VERIFY_TRUE(cond) VERIFY_TRUE_MSG(cond, "")
#define VERIFY_FALSE(cond) VERIFY_FALSE_MSG(cond, "")
#define VERIFY_EQ(cond1, cond2) VERIFY_EQ_MSG(cond1, cond2, "")
#define VERIFY_NE(cond1, cond2) VERIFY_NE_MSG(cond1, cond2, "")
#define VERIFY_OK(cond) VERIFY_OK_MSG(cond, "")

#endif  // CLIENT_SUITE_HARNESS
