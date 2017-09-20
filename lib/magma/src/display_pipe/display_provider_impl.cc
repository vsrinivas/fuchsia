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

void DisplayProviderImpl::GetInfo(const GetInfoCallback& callback) {
    auto info = display_pipe::DisplayInfo::New();
    uint32_t display_width, display_height;
    if (conn_->GetDisplaySize(&display_width, &display_height)) {
        info->width = display_width;
        info->height = display_height;
    } else {
        FXL_LOG(ERROR) << "Unable to query display size.";
        exit(1);
    }

    callback(std::move(info));
}

void DisplayProviderImpl::BindPipe(
    ::fidl::InterfaceRequest<scenic::ImagePipe> pipe) {
    image_pipe_.AddBinding(std::move(pipe));
}

void DisplayProviderImpl::AddBinding(
    fidl::InterfaceRequest<DisplayProvider> request) {
  bindings_.AddBinding(this, std::move(request));
}
};  // namespace display_pipe
