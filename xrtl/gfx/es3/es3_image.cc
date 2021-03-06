// Copyright 2017 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "xrtl/gfx/es3/es3_image.h"

#include <utility>

#include "xrtl/base/tracing.h"
#include "xrtl/gfx/es3/es3_image_view.h"
#include "xrtl/gfx/es3/es3_platform_context.h"
#include "xrtl/gfx/memory_heap.h"

namespace xrtl {
namespace gfx {
namespace es3 {

size_t ES3Image::ComputeAllocationSize(
    const Image::CreateParams& create_params) {
  size_t allocation_size = create_params.format.ComputeDataSize(
      create_params.size.width, create_params.size.height);
  switch (create_params.type) {
    case Image::Type::k2D:
      allocation_size *= create_params.mip_level_count;
      break;
    case Image::Type::kCube:
      allocation_size *= create_params.mip_level_count;
      allocation_size *= 6;
      break;
    case Image::Type::k2DArray:
      allocation_size *= create_params.mip_level_count;
      allocation_size *= create_params.array_layer_count;
      break;
    case Image::Type::k3D:
      allocation_size *= create_params.size.depth;
      allocation_size *= create_params.mip_level_count;
      break;
  }
  return allocation_size;
}

ES3Image::ES3Image(ES3ObjectLifetimeQueue* queue,
                   ref_ptr<MemoryHeap> memory_heap,
                   ES3TextureParams texture_params, size_t allocation_size,
                   CreateParams create_params)
    : Image(allocation_size, create_params),
      queue_(queue),
      memory_heap_(std::move(memory_heap)),
      texture_params_(texture_params) {
}

ES3Image::~ES3Image() = default;

void ES3Image::PrepareAllocation() { queue_->EnqueueObjectAllocation(this); }

void ES3Image::Release() {
  memory_heap_->ReleaseImage(this);
  queue_->EnqueueObjectDeallocation(this);
}

bool ES3Image::AllocateOnQueue() {
  WTF_SCOPE0("ES3Image#AllocateOnQueue");
  ES3PlatformContext::CheckHasContextLock();

  // TODO(benvanik): pool ID allocation.
  glGenTextures(1, &texture_id_);

  static const GLenum kTypeTarget[] = {
      GL_TEXTURE_2D,        // Image::Type::k2D
      GL_TEXTURE_2D_ARRAY,  // Image::Type::k2DArray
      GL_TEXTURE_3D,        // Image::Type::k3D
      GL_TEXTURE_CUBE_MAP,  // Image::Type::kCube
  };
  DCHECK_LT(static_cast<int>(type()), ABSL_ARRAYSIZE(kTypeTarget));
  target_ = kTypeTarget[static_cast<int>(type())];
  glBindTexture(target_, texture_id_);

  // Allocate storage for the texture data.
  switch (type()) {
    case Image::Type::k2D:
    case Image::Type::kCube:
      glTexStorage2D(target_, mip_level_count(),
                     texture_params_.internal_format, size().width,
                     size().height);
      break;
    case Image::Type::k2DArray:
      glTexStorage3D(target_, mip_level_count(),
                     texture_params_.internal_format, size().width,
                     size().height, array_layer_count());
      break;
    case Image::Type::k3D:
      glTexStorage3D(target_, mip_level_count(),
                     texture_params_.internal_format, size().width,
                     size().height, size().depth);
      break;
  }

  // Set default sampling parameters.
  // We'll use Sampler objects to perform the sampling, but to use the texture
  // as render target we need to ensure it doesn't have mip mapping set.
  glTexParameteri(target_, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(target_, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  glBindTexture(target_, 0);

  return true;
}

void ES3Image::DeallocateOnQueue() {
  WTF_SCOPE0("ES3Image#DeallocateOnQueue");
  ES3PlatformContext::CheckHasContextLock();
  if (texture_id_) {
    glDeleteTextures(1, &texture_id_);
    texture_id_ = 0;
  }
}

ref_ptr<MemoryHeap> ES3Image::memory_heap() const { return memory_heap_; }

ref_ptr<ImageView> ES3Image::CreateView() {
  return CreateView(create_params_.type, create_params_.format, entire_range());
}

ref_ptr<ImageView> ES3Image::CreateView(Image::Type type, PixelFormat format,
                                        Image::LayerRange layer_range) {
  return make_ref<ES3ImageView>(ref_ptr<Image>(this), type, format,
                                layer_range);
}

ref_ptr<ImageView> ES3Image::CreateView(Image::Type type, PixelFormat format) {
  return CreateView(type, format, entire_range());
}

void ES3Image::ReadDataRegionsOnQueue(
    absl::Span<const ReadImageRegion> data_regions) {
  WTF_SCOPE0("ES3Image#ReadDataRegionsOnQueue");
  ES3PlatformContext::CheckHasContextLock();

  for (const ReadImageRegion& data_region : data_regions) {
    const auto& source_range = data_region.source_layer_range;

    // TODO(benvanik): support automatically splitting across layers.
    DCHECK_EQ(1, source_range.layer_count);

    GLenum target = target_;
    if (type() == Type::kCube) {
      // Special cubemap handling, where layer index changes the target.
      DCHECK_LT(source_range.base_layer, 6);
      target = GL_TEXTURE_CUBE_MAP_POSITIVE_X + source_range.base_layer;
    }

    // TODO(benvanik): support arrays/3D textures.
    DCHECK(type() == Type::k2D || type() == Type::kCube);
    // TODO(benvanik): support compressed texture types.
    DCHECK_NE(texture_params_.type, GL_NONE);

    // TODO(benvanik): switch to PBOs.
    // Temporary FBO readback nastiness.
    GLuint framebuffer = 0;
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, target,
                           texture_id_, 0);
    glReadPixels(0, 0, size().width, size().height, texture_params_.format,
                 texture_params_.type, data_region.target_data);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, target, 0, 0);
    glDeleteFramebuffers(1, &framebuffer);

    // Flip the data we read vertically.
    size_t row_stride = format().ComputeDataSize(size().width, 1);
    int row_count = size().height;
    uint8_t* byte_data = reinterpret_cast<uint8_t*>(data_region.target_data);
    for (int y = 0; y < row_count / 2; ++y) {
      size_t src_offset_1 = y * row_stride;
      size_t src_offset_2 = (row_count - 1 - y) * row_stride;
      for (size_t i = 0; i < row_stride; ++i) {
        uint8_t t = byte_data[src_offset_1 + i];
        byte_data[src_offset_1 + i] = byte_data[src_offset_2 + i];
        byte_data[src_offset_2 + i] = t;
      }
    }
  }
}

void ES3Image::WriteDataRegionsOnQueue(
    absl::Span<const WriteImageRegion> data_regions) {
  WTF_SCOPE0("ES3Image#WriteData:queue");
  ES3PlatformContext::CheckHasContextLock();

  for (const WriteImageRegion& data_region : data_regions) {
    const auto& target_range = data_region.target_layer_range;
    // TODO(benvanik): support automatically splitting across layers.
    //                 We'll need to shift around in data for each layer.
    DCHECK_EQ(1, target_range.layer_count);

    GLenum target = target_;
    int level = target_range.mip_level;
    if (type() == Type::kCube) {
      // Special cubemap handling, where layer index changes the target.
      DCHECK_LT(target_range.base_layer, 6);
      target = GL_TEXTURE_CUBE_MAP_POSITIVE_X + target_range.base_layer;
    }

    // TODO(benvanik): support arrays/3D textures.
    DCHECK(type() == Type::k2D || type() == Type::kCube);
    // TODO(benvanik): support compressed texture types.
    DCHECK_NE(texture_params_.type, GL_NONE);

    // Upload image.
    glBindTexture(target, texture_id_);
    glTexSubImage2D(target, level, 0, 0, size().width, size().height,
                    texture_params_.format, texture_params_.type,
                    data_region.source_data);
    glBindTexture(target, 0);
  }
}

}  // namespace es3
}  // namespace gfx
}  // namespace xrtl
