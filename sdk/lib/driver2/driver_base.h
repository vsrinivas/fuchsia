// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DRIVER2_DRIVER_BASE_H_
#define LIB_DRIVER2_DRIVER_BASE_H_

#include <fidl/fuchsia.driver.framework/cpp/fidl.h>
#include <lib/driver2/driver_context.h>
#include <lib/driver2/logger.h>
#include <lib/driver2/namespace.h>
#include <lib/driver2/outgoing_directory.h>
#include <lib/driver2/record.h>
#include <lib/driver2/start_args.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <lib/sys/component/cpp/structured_config.h>

namespace driver {

using DriverStartArgs = fuchsia_driver_framework::DriverStartArgs;

// |DriverBase| is an interface that drivers should inherit from. It provides methods
// for accessing the start args, as well as helper methods for common initialization tasks.
//
// There are two virtual methods, |Start| which must be overridden,
// and |Stop| which is optional to override.
//
// In order to work with the default |BasicFactory| factory implementation,
// classes which inherit from |DriverBase| must implement a constructor with the following
// signature and forward said parameters to the |DriverBase| base class:
//
//   T(DriverStartArgs start_args, fdf::UnownedDispatcher driver_dispatcher);
//
// Otherwise a custom factory must be created and used to call constructors of any other shape.
//
// The following illustrates an example:
//
// ```
// class MyDriver : public driver::DriverBase {
//  public:
//   MyDriver(driver::DriverStartArgs start_args, fdf::UnownedDispatcher driver_dispatcher)
//       : driver::DriverBase("my_driver", std::move(start_args), std::move(driver_dispatcher)) {}
//
//   zx::status<> Start() override {
//     context().incoming()->Connect(...);
//     context().outgoing()->AddService(...);
//     FDF_LOG(INFO, "hello world!");
//     node_client_.Bind(std::move(node()), dispatcher());
//
//     /* Ensure all capabilities offered have been added to the outgoing directory first. */
//     auto add_result = node_client_->AddChild(...); if (add_result.is_error()) {
//       /* Releasing the node channel signals unbind to DF. */
//       node_client_.AsyncTeardown(); // Or node().reset() if we hadn't moved it into the client.
//       return add_result.take_error();
//     }
//
//     return zx::ok();
//   }
//  private:
//   fidl::SharedClient<fuchsia_driver_framework::Node> node_client_;
// };
// ```
class DriverBase {
 public:
  DriverBase(std::string_view name, DriverStartArgs start_args,
             fdf::UnownedDispatcher driver_dispatcher)
      : start_args_(std::move(start_args)),
        driver_dispatcher_(std::move(driver_dispatcher)),
        dispatcher_(driver_dispatcher_->async_dispatcher()),
        driver_context_(driver_dispatcher_->get()) {
    auto ns = std::move(start_args_.ns());
    ZX_ASSERT(ns.has_value());
    Namespace incoming = Namespace::Create(ns.value()).value();
    logger_ = Logger::Create(incoming, dispatcher_, name).value();

    auto outgoing_request = std::move(start_args_.outgoing_dir());
    ZX_ASSERT(outgoing_request.has_value());
    driver_context_.InitializeAndServe(std::move(incoming), std::move(outgoing_request.value()));
  }

  DriverBase(const DriverBase&) = delete;
  DriverBase& operator=(const DriverBase&) = delete;

  virtual ~DriverBase() = default;

  // This method will be called by the factory to start the driver. This is when
  // the driver should setup the outgoing directory through `context()->outgoing()->Add...` calls.
  // Do not call Serve, as it has already been called by the |DriverBase| constructor.
  // Child nodes can be created here synchronously or asynchronously as long as all of the
  // protocols being offered to the child has been added to the outgoing directory first.
  virtual zx::status<> Start() = 0;

  virtual void Stop() {}

  // This can be used to log in driver factories:
  // `FDF_LOGL(INFO, driver->logger(), "...");`
  Logger& logger() { return logger_; }

 protected:
  // The logger can't be private because the logging macros rely on it.
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes)
  Logger logger_;

  fidl::ClientEnd<fuchsia_driver_framework::Node>& node() {
    auto& node = start_args_.node();
    ZX_ASSERT(node.has_value());
    return node.value();
  }

  const fidl::ClientEnd<fuchsia_driver_framework::Node>& node() const {
    auto& node = start_args_.node();
    ZX_ASSERT(node.has_value());
    return node.value();
  }

  template <typename StructuredConfig>
  StructuredConfig take_config() {
    static_assert(component::IsDriverStructuredConfigV<StructuredConfig>,
                  "Invalid type supplied. StructuredConfig must be a driver flavored "
                  "structured config type. Example usage: take_config<my_driverconfig::Config>().");
    return StructuredConfig::TakeFromStartArgs(start_args_);
  }

  DriverContext& context() { return driver_context_; }
  const DriverContext& context() const { return driver_context_; }

  fdf::UnownedDispatcher& driver_dispatcher() { return driver_dispatcher_; }
  const fdf::UnownedDispatcher& driver_dispatcher() const { return driver_dispatcher_; }

  async_dispatcher_t* dispatcher() { return dispatcher_; }
  const async_dispatcher_t* dispatcher() const { return dispatcher_; }

  std::optional<fuchsia_data::Dictionary>& program() { return start_args_.program(); }
  const std::optional<fuchsia_data::Dictionary>& program() const { return start_args_.program(); }

  std::optional<std::string>& url() { return start_args_.url(); }
  const std::optional<std::string>& url() const { return start_args_.url(); }

  std::optional<std::string>& node_name() { return start_args_.node_name(); }
  const std::optional<std::string>& node_name() const { return start_args_.node_name(); }

  std::optional<std::vector<fuchsia_driver_framework::NodeSymbol>>& symbols() {
    return start_args_.symbols();
  }

  const std::optional<std::vector<fuchsia_driver_framework::NodeSymbol>>& symbols() const {
    return start_args_.symbols();
  }

 private:
  DriverStartArgs start_args_;
  fdf::UnownedDispatcher driver_dispatcher_;
  async_dispatcher_t* dispatcher_;
  DriverContext driver_context_;
};

// This is the default Factory that is used to Create a Driver of type |Driver|, that inherits the
// |DriverBase| class. |Driver| must implement a constructor with the following
// signature and forward said parameters to the |DriverBase| base class:
//
//   T(DriverStartArgs start_args, fdf::UnownedDispatcher driver_dispatcher);
template <typename Driver>
class BasicFactory {
  static_assert(std::is_base_of_v<DriverBase, Driver>, "Driver has to inherit from DriverBase");
  static_assert(std::is_constructible_v<Driver, DriverStartArgs, fdf::UnownedDispatcher>,
                "Driver must contain a constructor with the signature '(driver::DriverStartArgs, "
                "fdf::UnownedDispatcher)' in order to be used with the BasicFactory.");

 public:
  static zx::status<std::unique_ptr<DriverBase>> CreateDriver(
      DriverStartArgs start_args, fdf::UnownedDispatcher driver_dispatcher) {
    std::unique_ptr<DriverBase> driver =
        std::make_unique<Driver>(std::move(start_args), std::move(driver_dispatcher));
    auto result = driver->Start();
    if (result.is_error()) {
      FDF_LOGL(WARNING, driver->logger(), "Failed to Start driver: %s", result.status_string());
      return result.take_error();
    }

    return zx::ok(std::move(driver));
  }
};

// |Record| implements static |Start| and |Stop| methods which will be used by the framework.
//
// By default, it will utilize |BasicFactory| to construct your primary driver class, |Driver|,
// and invoke it's |Start| and |Stop| methods.
//
// |Driver| must inherit from |DriverBase|. If provided, |Factory| must implement a
// public |CreateDriver| function with the following signature:
// ```
// static zx::status<std::unique_ptr<DriverBase>> CreateDriver(
//     DriverStartArgs start_args, fdf::UnownedDispatcher driver_dispatcher)
// ```
//
// This illustrates how to use a |Record| with the default |BasicFactory|:
// ```
// FUCHSIA_DRIVER_RECORD_CPP_V2(driver::Record<MyDriver>);
// ```
//
// This illustrates how to use a |Record| with a custom factory:
// ```
// class CustomFactory {
//  public:
//   static zx::status<std::unique_ptr<DriverBase>> CreateDriver(
//       DriverStartArgs start_args, fdf::UnownedDispatcher driver_dispatcher)
//   ...construct and start driver...
// };
// // We must define the record before passing into the macro, otherwise the macro expansion
// // will think the comma is to pass a second macro argument.
// using record = driver::Record<MyDriver, CustomFactory>;
// FUCHSIA_DRIVER_RECORD_CPP_V2(record);
// ```
template <typename Driver, typename Factory = BasicFactory<Driver>>
class Record {
  static_assert(std::is_base_of_v<DriverBase, Driver>, "Driver has to inherit from DriverBase");

  DECLARE_HAS_MEMBER_FN(has_create_driver, CreateDriver);
  static_assert(has_create_driver_v<Factory>,
                "Factory must implement a public static CreateDriver function.");
  static_assert(
      std::is_same_v<decltype(&Factory::CreateDriver),
                     zx::status<std::unique_ptr<DriverBase>> (*)(
                         DriverStartArgs start_args, fdf::UnownedDispatcher driver_dispatcher)>,
      "CreateDriver must be a public static function with signature "
      "'zx::status<std::unique_ptr<driver::DriverBase>> (driver::DriverStartArgs start_args, "
      "fdf::UnownedDispatcher driver_dispatcher)'.");

 public:
  static zx_status_t Start(EncodedDriverStartArgs encoded_start_args, fdf_dispatcher_t* dispatcher,
                           void** driver) {
    // Decode the incoming `msg`.
    auto wire_format_metadata =
        fidl::WireFormatMetadata::FromOpaque(encoded_start_args.wire_format_metadata);
    fitx::result start_args = fidl::Decode<fuchsia_driver_framework::DriverStartArgs>(
        fidl::EncodedMessage::FromEncodedCMessage(encoded_start_args.msg), wire_format_metadata);
    if (!start_args.is_ok()) {
      return start_args.error_value().status();
    }

    zx::status<std::unique_ptr<DriverBase>> created_driver =
        Factory::CreateDriver(std::move(*start_args), fdf::UnownedDispatcher(dispatcher));

    if (created_driver.is_error()) {
      return created_driver.status_value();
    }

    // Store `driver` pointer.
    *driver = (*created_driver).release();
    return ZX_OK;
  }

  static zx_status_t Stop(void* driver) {
    DriverBase* casted_driver = static_cast<DriverBase*>(driver);
    casted_driver->Stop();
    delete casted_driver;
    return ZX_OK;
  }
};

#define FUCHSIA_DRIVER_RECORD_CPP_V2(record) \
  FUCHSIA_DRIVER_RECORD_V1(.start = record::Start, .stop = record::Stop)

}  // namespace driver

#endif  // LIB_DRIVER2_DRIVER_BASE_H_
