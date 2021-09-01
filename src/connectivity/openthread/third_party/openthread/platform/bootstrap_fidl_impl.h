#ifndef SRC_CONNECTIVITY_OPENTHREAD_THIRD_PARTY_OPENTHREAD_PLATFORM_BOOTSTRAP_FIDL_IMPL_H_
#define SRC_CONNECTIVITY_OPENTHREAD_THIRD_PARTY_OPENTHREAD_PLATFORM_BOOTSTRAP_FIDL_IMPL_H_

#include <fidl/fuchsia.lowpan.bootstrap/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/llcpp/server.h>

#include <string>

#include <fbl/ref_ptr.h>
#include <src/lib/storage/vfs/cpp/pseudo_dir.h>

namespace ot {
namespace Fuchsia {

/// Handler for all fuchsia.lowpan.bootstrap/Thread FIDL protocol calls. Registers as a
/// public service with the ComponentContext and handles incoming connections.
class BootstrapThreadImpl : public fidl::WireServer<fuchsia_lowpan_bootstrap::Thread> {
 public:
  // Construct a new instance of |BootstrapThreadImpl|.
  explicit BootstrapThreadImpl();

  ~BootstrapThreadImpl();

  // Bind this implementation to a server end of the channel.
  // |svc_dir| should be passed if server is added to it using AddEntry call.
  // This is used to RemoveEntry when FIDL channel is about to be closed.
  // |request| and |dispatcher| are needed for BindServer call.
  zx_status_t Bind(fidl::ServerEnd<fuchsia_lowpan_bootstrap::Thread> request,
                   async_dispatcher_t* dispatcher,
                   cpp17::optional<const fbl::RefPtr<fs::PseudoDir>> svc_dir);

  // Implementation of the fuchsia.lowpan.bootstrap.Thread interface.
  void ImportSettings(ImportSettingsRequestView request,
                      ImportSettingsCompleter::Sync& completer) override;

 private:
  // Prevent copy/move construction
  BootstrapThreadImpl(const BootstrapThreadImpl&) = delete;
  BootstrapThreadImpl(BootstrapThreadImpl&&) = delete;
  // Prevent copy/move assignment
  BootstrapThreadImpl& operator=(const BootstrapThreadImpl&) = delete;
  BootstrapThreadImpl& operator=(BootstrapThreadImpl&&) = delete;

  // Get the path to the settings file to be written by this implementation.
  virtual std::string GetSettingsPath();
  // Determine if this FIDL should be served.
  virtual bool ShouldServe();

  void StopServingFidl();
  void CloseBinding(zx_status_t close_bindings_status);

  // Closes binding using a completer. This should be used if there is a
  // communication in progress.
  void CloseBinding(zx_status_t close_bindings_status, ImportSettingsCompleter::Sync& completer);

  // A reference back to the Binding that this class is bound to, which is used
  // to send events to the client.
  cpp17::optional<fidl::ServerBindingRef<fuchsia_lowpan_bootstrap::Thread>> binding_;

  // If set, this is used to call RemoveEntry when closing down FIDL
  cpp17::optional<fbl::RefPtr<fs::PseudoDir>> svc_dir_;
};

}  // namespace Fuchsia
}  // namespace ot

#endif  // SRC_CONNECTIVITY_OPENTHREAD_THIRD_PARTY_OPENTHREAD_PLATFORM_BOOTSTRAP_FIDL_IMPL_H_
