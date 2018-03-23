#include "display_provider_impl.h"

#include "lib/fxl/logging.h"

namespace display_pipe {
DisplayProviderImpl::DisplayProviderImpl() :
    conn_(new MagmaConnection),
    image_pipe_(conn_)
    {
    if (!conn_->Open()) {
        FXL_LOG(ERROR) << "Unable to open connection to magma.";
        exit(1);
        return;
    }
}

DisplayProviderImpl::~DisplayProviderImpl() = default;

void DisplayProviderImpl::GetInfo(GetInfoCallback callback) {
    display_pipe::DisplayInfo info;
    if (!conn_->GetDisplaySize(&info.width, &info.height)) {
        FXL_LOG(ERROR) << "Unable to query display size.";
        exit(1);
    }

    callback(info);
}

void DisplayProviderImpl::BindPipe(
    ::fidl::InterfaceRequest<images::ImagePipe> pipe) {
    image_pipe_.AddBinding(std::move(pipe));
}

void DisplayProviderImpl::AddBinding(
    fidl::InterfaceRequest<DisplayProvider> request) {
  bindings_.AddBinding(this, std::move(request));
}
};  // namespace display_pipe
