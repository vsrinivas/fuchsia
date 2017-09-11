#include "image_pipe_impl.h"

#include "lib/fxl/logging.h"
#include "lib/mtl/tasks/message_loop.h"

namespace display_pipe {
ImagePipeImpl::ImagePipeImpl(std::shared_ptr<MagmaConnection> conn) : conn_(conn) {}

ImagePipeImpl::~ImagePipeImpl() = default;

void ImagePipeImpl::AddImage(uint32_t image_id, scenic::ImageInfoPtr image_info, mx::vmo memory,
                             scenic::MemoryType memory_type, uint64_t memory_offset)
{
    if (images_.find(image_id) != images_.end()) {
        FXL_LOG(ERROR) << "Image id " << image_id << " already added.";
        mtl::MessageLoop::GetCurrent()->PostQuitTask();
        return;
    }
    images_[image_id] = Image::Create(conn_, *image_info, std::move(memory), memory_offset);
}

void ImagePipeImpl::RemoveImage(uint32_t image_id)
{
    auto i = images_.find(image_id);
    if (i == images_.end()) {
        FXL_LOG(ERROR) << "Can't remove unknown image id " << image_id << ".";
        mtl::MessageLoop::GetCurrent()->PostQuitTask();
        return;
    }
    images_.erase(i);
}

void ImagePipeImpl::PresentImage(uint32_t image_id, uint64_t presentation_time,
                                 mx::event acquire_fence, mx::event release_fence,
                                 const PresentImageCallback& callback)
{
    auto i = images_.find(image_id);
    if (i == images_.end()) {
        FXL_LOG(ERROR) << "Can't present unknown image id " << image_id << ".";
        mtl::MessageLoop::GetCurrent()->PostQuitTask();
        return;
    }

    magma_semaphore_t buffer_presented_semaphore;
    if (!conn_->CreateSemaphore(&buffer_presented_semaphore)) {
        FXL_LOG(ERROR) << "Can't present unknown image id " << image_id << ".";
        mtl::MessageLoop::GetCurrent()->PostQuitTask();
        return;
    }

    magma_semaphore_t wait_semaphore;
    magma_semaphore_t signal_semaphore;
    conn_->ImportSemaphore(acquire_fence, &wait_semaphore);
    conn_->ImportSemaphore(release_fence, &signal_semaphore);

    conn_->DisplayPageFlip(i->second->buffer(), 1, &wait_semaphore, 1, &signal_semaphore,
                           buffer_presented_semaphore);

    conn_->ReleaseSemaphore(buffer_presented_semaphore);
    conn_->ReleaseSemaphore(wait_semaphore);
    conn_->ReleaseSemaphore(signal_semaphore);
    auto info = scenic::PresentationInfo::New();
    info->presentation_time = presentation_time;
    info->presentation_interval = 0;
    callback(std::move(info));
}

void ImagePipeImpl::AddBinding(fidl::InterfaceRequest<ImagePipe> request)
{
    bindings_.AddBinding(this, std::move(request));
}
}; // namespace display_pipe
