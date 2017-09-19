#include "image_pipe_impl.h"

#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/logging.h"

namespace display_pipe {
ImagePipeImpl::ImagePipeImpl(std::shared_ptr<MagmaConnection> conn) : conn_(conn) {}

ImagePipeImpl::~ImagePipeImpl() = default;

void ImagePipeImpl::AddImage(uint32_t image_id, scenic::ImageInfoPtr image_info, zx::vmo memory,
                             scenic::MemoryType memory_type, uint64_t memory_offset)
{
    if (images_.find(image_id) != images_.end()) {
        FXL_LOG(ERROR) << "Image id " << image_id << " already added.";
        fsl::MessageLoop::GetCurrent()->PostQuitTask();
        return;
    }
    images_[image_id] = Image::Create(conn_, *image_info, std::move(memory), memory_offset);
}

void ImagePipeImpl::RemoveImage(uint32_t image_id)
{
    auto i = images_.find(image_id);
    if (i == images_.end()) {
        FXL_LOG(ERROR) << "Can't remove unknown image id " << image_id << ".";
        fsl::MessageLoop::GetCurrent()->PostQuitTask();
        return;
    }
    images_.erase(i);
}

void ImagePipeImpl::PresentImage(uint32_t image_id,
                                 uint64_t presentation_time,
                                 ::fidl::Array<zx::event> acquire_fences,
                                 ::fidl::Array<zx::event> release_fences,
                                 const PresentImageCallback& callback) {
  auto i = images_.find(image_id);
  if (i == images_.end()) {
    FXL_LOG(ERROR) << "Can't present unknown image id " << image_id << ".";
    fsl::MessageLoop::GetCurrent()->PostQuitTask();
    return;
  }

  magma_semaphore_t buffer_presented_semaphore;
  if (!conn_->CreateSemaphore(&buffer_presented_semaphore)) {
    FXL_LOG(ERROR) << "Can't present unknown image id " << image_id << ".";
    fsl::MessageLoop::GetCurrent()->PostQuitTask();
    return;
  }

  std::vector<magma_semaphore_t> wait_semaphores;
  for (auto& acquire_fence : acquire_fences) {
    magma_semaphore_t wait_semaphore;
    conn_->ImportSemaphore(acquire_fence, &wait_semaphore);
    wait_semaphores.push_back(wait_semaphore);
  }

  std::vector<magma_semaphore_t> signal_semaphores;
  for (auto& release_fence : release_fences) {
    magma_semaphore_t signal_semaphore;
    conn_->ImportSemaphore(release_fence, &signal_semaphore);
    signal_semaphores.push_back(signal_semaphore);
  }

  conn_->DisplayPageFlip(i->second->buffer(), wait_semaphores.size(),
                         wait_semaphores.data(), signal_semaphores.size(),
                         signal_semaphores.data(), buffer_presented_semaphore);

  conn_->ReleaseSemaphore(buffer_presented_semaphore);

  for (auto wait_semaphore : wait_semaphores)
    conn_->ReleaseSemaphore(wait_semaphore);

  for (auto signal_semaphore : signal_semaphores)
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
