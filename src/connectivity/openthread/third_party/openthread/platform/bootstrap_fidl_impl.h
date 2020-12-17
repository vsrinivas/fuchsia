#ifndef SRC_CONNECTIVITY_OPENTHREAD_THIRD_PARTY_OPENTHREAD_PLATFORM_BOOTSTRAP_FIDL_IMPL_H_
#define SRC_CONNECTIVITY_OPENTHREAD_THIRD_PARTY_OPENTHREAD_PLATFORM_BOOTSTRAP_FIDL_IMPL_H_

#include <fuchsia/thread/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

#include <string>

namespace ot {
namespace Fuchsia {

/// Handler for all fuchsia.thread/Bootstrap FIDL protocol calls. Registers as a
/// public service with the ComponentContext and handles incoming connections.
class BootstrapImpl : public fuchsia::thread::Bootstrap {
 public:
  // Construct a new instance of |BootstrapImpl|.
  // This method does not take ownership of the context.
  explicit BootstrapImpl(sys::ComponentContext* context);
  ~BootstrapImpl();

  // Initialize and register this instance as FIDL handler.
  zx_status_t Init();

  // Implementation of the fuchsia.thread.Bootstrap interface.
  void ImportThreadSettings(fuchsia::mem::Buffer thread_settings_json,
                            ImportThreadSettingsCallback callback) override;

 private:
  // Prevent copy/move construction
  BootstrapImpl(const BootstrapImpl&) = delete;
  BootstrapImpl(BootstrapImpl&&) = delete;
  // Prevent copy/move assignment
  BootstrapImpl& operator=(const BootstrapImpl&) = delete;
  BootstrapImpl& operator=(BootstrapImpl&&) = delete;

  // Get the path to the settings file to be written by this implementation.
  virtual std::string GetSettingsPath();
  // Determine if this FIDL should be served.
  virtual bool ShouldServe();

  void StopServingFidl();
  void StopServingFidlAndCloseBindings(zx_status_t close_bindings_status);

  // FIDL servicing related state
  fidl::BindingSet<fuchsia::thread::Bootstrap> bindings_;
  sys::ComponentContext* context_;
  bool serving_ = false;
};

}  // namespace Fuchsia
}  // namespace ot

#endif  // SRC_CONNECTIVITY_OPENTHREAD_THIRD_PARTY_OPENTHREAD_PLATFORM_BOOTSTRAP_FIDL_IMPL_H_
