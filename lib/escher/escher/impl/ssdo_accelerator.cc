// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/ssdo_accelerator.h"

#include "escher/impl/image_cache.h"
#include "escher/impl/command_buffer.h"
#include "escher/renderer/image.h"
#include "escher/renderer/texture.h"
#include "escher/resources/resource_life_preserver.h"

namespace escher {
namespace impl {

namespace {

constexpr char g_high_low_neighbors_packed_kernel_src[] = R"GLSL(
#version 450
#extension GL_ARB_separate_shader_objects : enable

layout (binding = 0) uniform sampler2D depthImage;
layout (binding = 1, rgba8) uniform image2D resultImage;

const int kSize = 8;
void main() {
  float depths[kSize + 2][kSize + 2];
  uint x = gl_GlobalInvocationID.x * kSize;
  uint y = gl_GlobalInvocationID.y * kSize;

  for (uint i = 0; i < kSize + 2; ++i) {
    for (uint j = 0; j < kSize + 2; ++j) {
      depths[i][j] = texture(depthImage, ivec2(x + i - 1, y + j - 1)).r;
    }
  }

  vec4 zero = vec4(0.f, 0.f, 0.f, 0.f);
  for (uint i = 0; i < kSize / 4; ++i) {
    for (uint j = 0; j < kSize / 4; ++j) {
      ivec4 packed_output = ivec4(0, 0, 0, 0);
      for (uint row = 0; row < 4; ++row) {
        for (uint col = 0; col < 4; ++col) {
          // The center of a 3x3 neighborhood of interest.
          ivec2 pos = ivec2(i * 4 + col + 1, j * 4 + row + 1);

          // TODO: this is slower than the unvectorized code below on
          // NVIDIA/Linux.  Compare on Fuchsia.
          /*
          float cd = depths[pos.x][pos.y];
          vec4 center_depths = vec4(cd, cd, cd, cd);

          vec4 difference = center_depths - vec4(depths[pos.x - 1][pos.y - 1],
                                                 depths[pos.x - 1][pos.y],
                                                 depths[pos.x - 1][pos.y + 1],
                                                 depths[pos.x][pos.y - 1]);
          bool higher_neighbor = any(greaterThan(difference, zero));
          bool lower_neighbor = any(lessThan(difference, zero));

          difference = center_depths - vec4(depths[pos.x][pos.y + 1],
                                            depths[pos.x + 1][pos.y - 1],
                                            depths[pos.x + 1][pos.y],
                                            depths[pos.x + 1][pos.y + 1]);

          higher_neighbor = higher_neighbor ||
              any(greaterThan(difference, vec4(0.f, 0.f, 0.f, 0.f)));
          lower_neighbor = lower_neighbor ||
              any(lessThan(difference, vec4(0.f, 0.f, 0.f, 0.f)));

          int bits = (higher_neighbor ? 1 : 0) +
                     (lower_neighbor ? 2 : 0);
          packed_output[row] += (bits << (col * 2));
          */

          float center_depth = depths[pos.x][pos.y];

          // Determine whether the current pixel has a higher and/or lower
          // neighbor.  This is done by accumulating positive/negative depth
          // differences (more performant than e.g. boolean operations).
          bool higher_neighbor = false;
          bool lower_neighbor = false;
          float difference;
          difference = center_depth - depths[pos.x - 1][pos.y - 1];
          higher_neighbor = higher_neighbor || (difference > 0.f);
          lower_neighbor = lower_neighbor || (difference < 0.f);

          difference = center_depth - depths[pos.x - 1][pos.y];
          higher_neighbor = higher_neighbor || (difference > 0.f);
          lower_neighbor = lower_neighbor || (difference < 0.f);

          difference = center_depth - depths[pos.x - 1][pos.y + 1];
          higher_neighbor = higher_neighbor || (difference > 0.f);
          lower_neighbor = lower_neighbor || (difference < 0.f);

          difference = center_depth - depths[pos.x][pos.y - 1];
          higher_neighbor = higher_neighbor || (difference > 0.f);
          lower_neighbor = lower_neighbor || (difference < 0.f);

          difference = center_depth - depths[pos.x][pos.y + 1];
          higher_neighbor = higher_neighbor || (difference > 0.f);
          lower_neighbor = lower_neighbor || (difference < 0.f);

          difference = center_depth - depths[pos.x + 1][pos.y - 1];
          higher_neighbor = higher_neighbor || (difference > 0.f);
          lower_neighbor = lower_neighbor || (difference < 0.f);

          difference = center_depth - depths[pos.x + 1][pos.y];
          higher_neighbor = higher_neighbor || (difference > 0.f);
          lower_neighbor = lower_neighbor || (difference < 0.f);

          difference = center_depth - depths[pos.x + 1][pos.y + 1];
          higher_neighbor = higher_neighbor || (difference > 0.f);
          lower_neighbor = lower_neighbor || (difference < 0.f);

          int bits = (higher_neighbor ? 1 : 0) +
                     (lower_neighbor ? 2 : 0);
          packed_output[row] += (bits << (col * 2));
        }
      }

      imageStore(resultImage,
                 ivec2(x / 4 + i, y / 4 + j),
                 vec4(packed_output) / 255.f);
    }
  }
}
)GLSL";

constexpr char g_high_low_neighbors_packed_parallel_kernel_src[] = R"GLSL(
#version 450
#extension GL_ARB_separate_shader_objects : enable

// Square root of the number of threads in the workgroup.
const int kWorkgroupThreadDim = 2;
layout(local_size_x = kWorkgroupThreadDim, local_size_y = kWorkgroupThreadDim) in;

layout (binding = 0) uniform sampler2D depthImage;
layout (binding = 1, rgba8) uniform image2D resultImage;

// Length of an edge of the square region of pixels that is collectively
// processed by all threads in the workgroup.
const int kRegionSize = kWorkgroupThreadDim * 4;

// The algorithm requires depth values not only for the region of interest, but
// also a 1-pixel 'buffer zone' around it.
shared lowp float depths[kRegionSize + 2][kRegionSize + 2];

// Loads the depth values that are processed by all threads in this workgroup.
void loadDepthValues() {
  if (kWorkgroupThreadDim == 2) {
    // Use a specialized version when there are 4 threads.  Each thread loads
    // 1/4 of the depth region that is shared between workgroup threads.
    ivec2 offset = ivec2(5 * gl_LocalInvocationID.xy);
    ivec2 depth_load_region_top_left =
        4 * ivec2(gl_GlobalInvocationID.xy - gl_LocalInvocationID.xy)
        + offset
        - ivec2(1, 1);
    for (uint i = 0; i < 5; ++i) {
      for (uint j = 0; j < 5; ++j) {
        depths[i + offset.x][j + offset.y] =
            texture(depthImage,
                    depth_load_region_top_left + ivec2(i, j)).r;
      }
    }
  } else {
    // We designate one thread as the leader; it is responsible for loading all
    // required depth values while the other threads wait.
    if (gl_LocalInvocationID.x == 0 && gl_LocalInvocationID.y == 0) {
      ivec2 depth_load_region_top_left = ivec2(gl_GlobalInvocationID.x * 4 - 1,
                                               gl_GlobalInvocationID.y * 4 - 1);
      for (uint i = 0; i < kRegionSize + 2; ++i) {
        for (uint j = 0; j < kRegionSize + 2; ++j) {
          depths[i][j] = texture(depthImage,
                                 depth_load_region_top_left + ivec2(i, j)).r;
        }
      }
    }
  }
}

void main() {
  loadDepthValues();
  memoryBarrierShared();

  // Each thread is responsible for processing a 4x4 region, and packing the
  // 16 2-bit result values into a single pixel.
  ivec4 packed_output = ivec4(0, 0, 0, 0);
  for (uint row = 0; row < 4; ++row) {
    for (uint col = 0; col < 4; ++col) {
      // The center of a 3x3 neighborhood of interest.
      uvec2 pos = gl_LocalInvocationID.xy * 4 + uvec2(col + 1, row + 1);

      float center_depth = depths[pos.x][pos.y];

      // Determine whether the current pixel has a higher and/or lower
      // neighbor.  This is done by accumulating positive/negative depth
      // differences (more performant than e.g. boolean operations).
      bool higher_neighbor = false;
      bool lower_neighbor = false;
      float difference;
      difference = center_depth - depths[pos.x - 1][pos.y - 1];
      higher_neighbor = higher_neighbor || (difference > 0.f);
      lower_neighbor = lower_neighbor || (difference < 0.f);

      difference = center_depth - depths[pos.x - 1][pos.y];
      higher_neighbor = higher_neighbor || (difference > 0.f);
      lower_neighbor = lower_neighbor || (difference < 0.f);

      difference = center_depth - depths[pos.x - 1][pos.y + 1];
      higher_neighbor = higher_neighbor || (difference > 0.f);
      lower_neighbor = lower_neighbor || (difference < 0.f);

      difference = center_depth - depths[pos.x][pos.y - 1];
      higher_neighbor = higher_neighbor || (difference > 0.f);
      lower_neighbor = lower_neighbor || (difference < 0.f);

      difference = center_depth - depths[pos.x][pos.y + 1];
      higher_neighbor = higher_neighbor || (difference > 0.f);
      lower_neighbor = lower_neighbor || (difference < 0.f);

      difference = center_depth - depths[pos.x + 1][pos.y - 1];
      higher_neighbor = higher_neighbor || (difference > 0.f);
      lower_neighbor = lower_neighbor || (difference < 0.f);

      difference = center_depth - depths[pos.x + 1][pos.y];
      higher_neighbor = higher_neighbor || (difference > 0.f);
      lower_neighbor = lower_neighbor || (difference < 0.f);

      difference = center_depth - depths[pos.x + 1][pos.y + 1];
      higher_neighbor = higher_neighbor || (difference > 0.f);
      lower_neighbor = lower_neighbor || (difference < 0.f);

      int bits = (higher_neighbor ? 1 : 0) +
                 (lower_neighbor ? 2 : 0);
      packed_output[row] += (bits << (col * 2));
    }
  }

  imageStore(resultImage,
             ivec2(gl_GlobalInvocationID.xy),
             vec4(packed_output) / 255.f);
}
)GLSL";

constexpr char g_sampling_filtering_packed_kernel_src[] = R"GLSL(
#version 450
#extension GL_ARB_separate_shader_objects : enable

layout (binding = 0) uniform sampler2D depthImage;
layout (binding = 1, rgba8) uniform image2D resultImage;

// Must be <= 4, otherwise margins will be too small.  See below.
const int kRadius = 4;

// The area of interest is 8-wide so that we can pack the bits into a channel
// of an 8-bit image.  It is 8-high just because.
const int kEight = 8;

// Holds the 4-way neighbor relationships for the area of interest, plus a
// 8-wide kRadius-high area above and below.
//
// A 1-bit in the
//                R channel means that the cell is higher than its up-neighbor.
//                G channel means that the cell is higher than its down-neighbor.
//                B channel means that the cell is higher than its left-neighbor.
//                A channel means that the cell is higher than its right-neighbor.
const int kNeighborhoodHeight = kEight + 2 * kRadius;
const int kNeighborhoodWidth = kEight + 2 * kRadius;

// NOTE: we put the two variables below in shared memory even though we have a
// single thread per workgroup, in order to avoid exceeding the available
// number of registers.  On a particular NVIDIA GPU, making 'roi' shared is
// enough, and making 'depths' shared actually reduces performance.  However, on
// the Acer Switch 12 Alpha, both are necessary to avoid VK_ERROR_DEVICE_LOST.

// The 'region of interest' that stores the intermediate data structure.
shared ivec4 roi[kNeighborhoodHeight];

void computeNeighborRelationships() {
  float depths0[kNeighborhoodWidth];
  float depths1[kNeighborhoodWidth];

  ivec2 depth_base =
      ivec2(gl_GlobalInvocationID.xy) * kEight - ivec2(kRadius, kRadius);

  for (uint x = 0; x < kNeighborhoodWidth; ++x) {
    // Load into depths1.  It will be copied into depths0 before using
    // (wasteful, but makes the code cleaner), hence the 'y = 0' below.
    depths1[x] = texture(depthImage, depth_base + ivec2(x, 0)).r;
  }

  // Bottom row doesn't cast shadows downward, and top row doesn't cast
  // shadows upward.
  roi[kNeighborhoodHeight - 1].g = 0;
  roi[0].r = 0;

  // Compute leftward/rightward shadow-casting for top row.
  {
    int casts_rightward = 0;
    int casts_leftward = 0;

    /*  What follows is a vectorized version of this code:
        for (uint x = 1; x < kNeighborhoodWidth; ++x) {
          float diff = depths1[(x - 1)] - depths1[x];
          casts_rightward += (diff < 0.0) ? (1 << x) : 0;
          casts_leftward += (diff > 0.0) ? (2 << x) : 0;
        }
    */

    vec4 diff1 = vec4(depths1[0], depths1[1], depths1[2], depths1[3]);
    vec4 diff2 = vec4(depths1[1], depths1[2], depths1[3], depths1[4]);
    diff1 -= diff2;
    casts_rightward += int(dot(ivec4(1, 2, 4, 8),
                               mix(ivec4(0, 0, 0, 0),
                                   ivec4(1, 1, 1, 1),
                                   lessThan(diff1, vec4(0, 0, 0, 0)))));
    casts_leftward += int(dot(ivec4(1, 2, 4, 8),
                              mix(ivec4(0, 0, 0, 0),
                                  ivec4(1, 1, 1, 1),
                                  greaterThan(diff1, vec4(0, 0, 0, 0))))) << 1;
    diff1 = vec4(depths1[4], depths1[5], depths1[6], depths1[7]);
    diff2 = vec4(depths1[5], depths1[6], depths1[7], depths1[8]);
    diff1 -= diff2;
    casts_rightward += int(dot(ivec4(1, 2, 4, 8),
                               mix(ivec4(0, 0, 0, 0),
                                   ivec4(1, 1, 1, 1),
                                   lessThan(diff1, vec4(0, 0, 0, 0))))) << 4;
    casts_leftward += int(dot(ivec4(1, 2, 4, 8),
                              mix(ivec4(0, 0, 0, 0),
                                  ivec4(1, 1, 1, 1),
                                  greaterThan(diff1, vec4(0, 0, 0, 0))))) << 5;
    diff1 = vec4(depths1[8], depths1[9], depths1[10], depths1[11]);
    diff2 = vec4(depths1[9], depths1[10], depths1[11], depths1[12]);
    diff1 -= diff2;
    casts_rightward += int(dot(ivec4(1, 2, 4, 8),
                               mix(ivec4(0, 0, 0, 0),
                                   ivec4(1, 1, 1, 1),
                                   lessThan(diff1, vec4(0, 0, 0, 0))))) << 8;
    casts_leftward += int(dot(ivec4(1, 2, 4, 8),
                              mix(ivec4(0, 0, 0, 0),
                                  ivec4(1, 1, 1, 1),
                                  greaterThan(diff1, vec4(0, 0, 0, 0))))) << 9;
    diff1 = vec4(depths1[12], depths1[13], depths1[14], depths1[15]);
    // Note that the last value is repeated, to not go out-of-bounds.
    diff2 = vec4(depths1[13], depths1[14], depths1[15], depths1[15]);
    diff1 -= diff2;
    casts_rightward += int(dot(ivec4(1, 2, 4, 8),
                               mix(ivec4(0, 0, 0, 0),
                                   ivec4(1, 1, 1, 1),
                                   lessThan(diff1, vec4(0, 0, 0, 0))))) << 12;
    casts_leftward += int(dot(ivec4(1, 2, 4, 8),
                              mix(ivec4(0, 0, 0, 0),
                                  ivec4(1, 1, 1, 1),
                                  greaterThan(diff1, vec4(0, 0, 0, 0))))) << 13;

    roi[0].b = casts_leftward;
    roi[0].a = casts_rightward;
  }

  for (uint y = 1; y < kNeighborhoodHeight; ++y) {
    // Update depth values for 2-row subneighborhood, and compute upward/
    // downward shadow-casting.
    {
      int casts_upward = 0;
      int casts_downward = 0;

      /*  What follows is a vectorized version of this code:
          for (uint x = 0; x < kNeighborhoodWidth; ++x) {
            depths0[x] = depths1[x];
            depths1[x] = texture(depthImage, depth_base + ivec2(x, y)).r;

            float diff = depths1[x] - depths0[x];
            casts_downward += (diff > 0.0) ? (1 << x) : 0;
            casts_upward += (diff < 0.0) ? (1 << x) : 0;
          }
      */

      for (uint x = 0; x < kNeighborhoodWidth; x += 4) {
        depths0[x] = depths1[x];
        depths0[x + 1] = depths1[x + 1];
        depths0[x + 2] = depths1[x + 2];
        depths0[x + 3] = depths1[x + 3];
        depths1[x] = texture(depthImage, depth_base + ivec2(x, y)).r;
        depths1[x + 1] = texture(depthImage, depth_base + ivec2(x + 1, y)).r;
        depths1[x + 2] = texture(depthImage, depth_base + ivec2(x + 2, y)).r;
        depths1[x + 3] = texture(depthImage, depth_base + ivec2(x + 3, y)).r;

        vec4 diff = vec4(depths1[x], depths1[x + 1], depths1[x + 2], depths1[x + 3]) -
                    vec4(depths0[x], depths0[x + 1], depths0[x + 2], depths0[x + 3]);
        casts_downward += int(dot(ivec4(1, 2, 4, 8),
                                  mix(ivec4(0, 0, 0, 0),
                                      ivec4(1, 1, 1, 1),
                                      greaterThan(diff, vec4(0, 0, 0, 0))))) << x;
        casts_upward += int(dot(ivec4(1, 2, 4, 8),
                                mix(ivec4(0, 0, 0, 0),
                                    ivec4(1, 1, 1, 1),
                                    lessThan(diff, vec4(0, 0, 0, 0))))) << x;
      }

      roi[y - 1].g = casts_downward;
      roi[y].r = casts_upward;
    }

    // Compute leftward/rightward shadow casting for current row.
    {
      int casts_rightward = 0;
      int casts_leftward = 0;

      /*  What follows is a vectorized version of this code:
          for (uint x = 1; x < kNeighborhoodWidth; ++x) {
            float diff = depths1[(x - 1)] - depths1[x];
            casts_rightward += (diff < 0.0) ? (1 << x) : 0;
            casts_leftward += (diff > 0.0) ? (2 << x) : 0;
          }
      */

      vec4 diff1 = vec4(depths1[0], depths1[1], depths1[2], depths1[3]);
      vec4 diff2 = vec4(depths1[1], depths1[2], depths1[3], depths1[4]);
      diff1 -= diff2;
      casts_rightward += int(dot(ivec4(1, 2, 4, 8),
                                 mix(ivec4(0, 0, 0, 0),
                                     ivec4(1, 1, 1, 1),
                                     lessThan(diff1, vec4(0, 0, 0, 0)))));
      casts_leftward += int(dot(ivec4(1, 2, 4, 8),
                                mix(ivec4(0, 0, 0, 0),
                                    ivec4(1, 1, 1, 1),
                                    greaterThan(diff1, vec4(0, 0, 0, 0))))) << 1;
      diff1 = vec4(depths1[4], depths1[5], depths1[6], depths1[7]);
      diff2 = vec4(depths1[5], depths1[6], depths1[7], depths1[8]);
      diff1 -= diff2;
      casts_rightward += int(dot(ivec4(1, 2, 4, 8),
                                 mix(ivec4(0, 0, 0, 0),
                                     ivec4(1, 1, 1, 1),
                                     lessThan(diff1, vec4(0, 0, 0, 0))))) << 4;
      casts_leftward += int(dot(ivec4(1, 2, 4, 8),
                                mix(ivec4(0, 0, 0, 0),
                                    ivec4(1, 1, 1, 1),
                                    greaterThan(diff1, vec4(0, 0, 0, 0))))) << 5;
      diff1 = vec4(depths1[8], depths1[9], depths1[10], depths1[11]);
      diff2 = vec4(diff1.gba, depths1[12]);
      diff1 -= diff2;
      casts_rightward += int(dot(ivec4(1, 2, 4, 8),
                                 mix(ivec4(0, 0, 0, 0),
                                     ivec4(1, 1, 1, 1),
                                     lessThan(diff1, vec4(0, 0, 0, 0))))) << 8;
      casts_leftward += int(dot(ivec4(1, 2, 4, 8),
                                mix(ivec4(0, 0, 0, 0),
                                    ivec4(1, 1, 1, 1),
                                    greaterThan(diff1, vec4(0, 0, 0, 0))))) << 9;
      diff1 = vec4(depths1[12], depths1[13], depths1[14], depths1[15]);
      // Note that the last value is repeated, to not go out-of-bounds.
      diff2 = vec4(depths1[13], depths1[14], depths1[15], depths1[15]);
      diff1 -= diff2;
      casts_rightward += int(dot(ivec4(1, 2, 4, 8),
                                 mix(ivec4(0, 0, 0, 0),
                                     ivec4(1, 1, 1, 1),
                                     lessThan(diff1, vec4(0, 0, 0, 0))))) << 12;
      casts_leftward += int(dot(ivec4(1, 2, 4, 8),
                                mix(ivec4(0, 0, 0, 0),
                                    ivec4(1, 1, 1, 1),
                                    greaterThan(diff1, vec4(0, 0, 0, 0))))) << 13;

      roi[y].b = casts_leftward;
      roi[y].a = casts_rightward;
    }
  }
}

void smearNeighborRelationships() {
  // Smear 'downward' to cast shadows even further 'downward'.
  // Count downward so that we don't smear already-smeared values.
  for (int y = kEight - 1; y >= 0; --y) {
    ivec4 smeared = roi[kRadius + y];
    for (uint rad = 1; rad < kRadius; ++rad) {
      smeared.gba |= roi[kRadius + y - rad].gba;
    }
    roi[kRadius + y] = smeared;
  }

  // Smear 'upward' to cast shadows even further 'upward'.
  for (uint y = 0; y < kEight; ++y) {
    ivec3 smeared = roi[kRadius + y].rba;
    for (uint rad = 1; rad < kRadius; ++rad) {
      smeared |= roi[kRadius + y + rad].rba;
    }
    roi[kRadius + y].rba = smeared;
  }

  // Smear 'rightward' to cast shadows even further 'rightward', and similarly
  // for leftward.
  for (uint y = 0; y < kEight; ++y) {
    ivec4 smeared = roi[kRadius + y];
    for (uint rad = 1; rad < kRadius; ++rad) {
      // Smear 'upward' and 'downward' bits to left and right.
      // Smear 'leftward' bits to left only, to avoid false positives.
      // Smear 'rightward' bits to right only, to avoid false positives.
      smeared.rgb |= (smeared.rgb >> 1);
      smeared.rga |= (smeared.rga << 1);
    }
    roi[kRadius + y] = smeared;
  }
}

void main() {
  computeNeighborRelationships();
  smearNeighborRelationships();

  ivec2 base = ivec2(gl_GlobalInvocationID.xy) * 2;
  for (int y = 0; y < 2; ++y) {
    ivec4 up_down_row = ivec4(
        (roi[y * 4 + kRadius].r | roi[y * 4 + kRadius].g) >> kRadius,
        (roi[y * 4 + kRadius + 1].r | roi[y * 4 + kRadius + 1].g) >> kRadius,
        (roi[y * 4 + kRadius + 2].r | roi[y * 4 + kRadius + 2].g) >> kRadius,
        (roi[y * 4 + kRadius + 3].r | roi[y * 4 + kRadius + 3].g) >> kRadius);

    ivec4 left_right_row = ivec4(
        (roi[y * 4 + kRadius].b | roi[y * 4 + kRadius].a) >> kRadius,
        (roi[y * 4 + kRadius + 1].b | roi[y * 4 + kRadius + 1].a) >> kRadius,
        (roi[y * 4 + kRadius + 2].b | roi[y * 4 + kRadius + 2].a) >> kRadius,
        (roi[y * 4 + kRadius + 3].b | roi[y * 4 + kRadius + 3].a) >> kRadius);

    ivec4 left_row = ivec4(0, 0, 0, 0);
    ivec4 right_row = ivec4(0, 0, 0, 0);

    for (int xx = 0; xx < 4; ++xx) {
      for (int yy = 0; yy < 4; ++yy) {
        left_row[yy] += (up_down_row[yy] & (1 << xx)) > 0 ? (1 << (xx * 2)) : 0;
        left_row[yy] += (left_right_row[yy] & (1 << xx)) > 0 ? (1 << (xx * 2 + 1)) : 0;
        right_row[yy] += (up_down_row[yy] & (1 << (xx + 4))) > 0 ? (1 << (xx * 2)) : 0;
        right_row[yy] += (left_right_row[yy] & (1 << (xx + 4))) > 0 ? (1 << (xx * 2 + 1)) : 0;
      }
    }

    imageStore(resultImage, base + ivec2(0, y), vec4(left_row) / 255.0);
    imageStore(resultImage, base + ivec2(1, y), vec4(right_row) / 255.0);
  }
}
)GLSL";

constexpr char g_null_packed_kernel_src[] = R"GLSL(
#version 450
#extension GL_ARB_separate_shader_objects : enable

layout (binding = 0) uniform sampler2D depthImage;
layout (binding = 1, rgba8) uniform image2D resultImage;

void main() {
  ivec2 base = ivec2(gl_GlobalInvocationID.xy) * 2;
  for (int y = 0; y < 2; ++y) {
    imageStore(resultImage, base + ivec2(0, y), vec4(1.0, 1.0, 1.0, 1.0));
    imageStore(resultImage, base + ivec2(1, y), vec4(1.0, 1.0, 1.0, 1.0));
  }
}
)GLSL";

constexpr char g_unpack_32_to_2_kernel_src[] = R"GLSL(
#version 450
#extension GL_ARB_separate_shader_objects : enable

layout (binding = 0, rgba8) uniform image2D tmpImage;
layout (binding = 1, rgba8) uniform image2D resultImage;

const int kSize = 8;
void main() {
  uint x = gl_GlobalInvocationID.x * kSize;
  uint y = gl_GlobalInvocationID.y * kSize;

  for (uint i = 0; i < kSize / 4; ++i) {
    for (uint j = 0; j < kSize / 4; ++j) {
      // TODO: can we directly load ivec4?
      ivec4 block = ivec4(imageLoad(tmpImage, ivec2(x / 4 + i, y / 4 + j)) * 255.f);
      for (uint xx = 0; xx < 4; ++xx) {
        for (uint yy = 0; yy < 4; ++yy) {
          imageStore(resultImage,
                     ivec2(x + i * 4 + xx, y + j * 4 + yy),
                     vec4(
                       ((block[yy] >> (2 * xx)) & 1),
                       ((block[yy] >> (2 * xx + 1)) & 1),
                       0.f, 1.f));
        }
      }
    }
  }
}
)GLSL";

}  // namespace

SsdoAccelerator::SsdoAccelerator(GlslToSpirvCompiler* compiler,
                                 ImageCache* image_cache,
                                 ResourceLifePreserver* life_preserver)
    : device_(life_preserver->vulkan_context().device),
      compiler_(compiler),
      image_cache_(image_cache),
      life_preserver_(life_preserver) {}

SsdoAccelerator::~SsdoAccelerator() {}

const VulkanContext& SsdoAccelerator::vulkan_context() const {
  return life_preserver_->vulkan_context();
}

TexturePtr SsdoAccelerator::GenerateLookupTable(CommandBuffer* command_buffer,
                                                const TexturePtr& depth_texture,
                                                vk::ImageUsageFlags image_flags,
                                                Timestamper* timestamper) {
  switch (mode_) {
    case 0:
      return GenerateNullLookupTable(command_buffer, depth_texture, image_flags,
                                     timestamper);
    case 1:
      return GenerateSampleLookupTable(command_buffer, depth_texture,
                                       image_flags, timestamper);
    // There are "parallel" and "sequential" variations of the high/low table
    // generation kernel.
    case 2:
    case 3:
      return GenerateHighLowLookupTable(command_buffer, depth_texture,
                                        image_flags, timestamper);
    default:
      FTL_CHECK(false);
      return TexturePtr();
  }
}

TexturePtr SsdoAccelerator::GenerateSampleLookupTable(
    CommandBuffer* command_buffer,
    const TexturePtr& depth_texture,
    vk::ImageUsageFlags image_flags,
    Timestamper* timestamper) {
  uint32_t width = depth_texture->width();
  uint32_t height = depth_texture->height();

  // Size of neighborhood of pixels to work on for each invocation of the
  // compute kernel.  Must match the value in the compute shader source code,
  // and be a multiple of 4.
  constexpr uint32_t kSize = 8;

  uint32_t work_groups_x = width / kSize + (width % kSize > 0 ? 1 : 0);
  uint32_t work_groups_y = height / kSize + (height % kSize > 0 ? 1 : 0);

  uint32_t packed_width = width / 4 + (width % kSize > 0 ? 1 : 0);
  uint32_t packed_height = height / 4 + (height % kSize > 0 ? 1 : 0);

  ImagePtr tmp_image = image_cache_->NewImage(
      {vk::Format::eR8G8B8A8Unorm, packed_width, packed_height, 1,
       image_flags | vk::ImageUsageFlagBits::eStorage});
  TexturePtr tmp_texture = ftl::MakeRefCounted<Texture>(
      life_preserver_, tmp_image, vk::Filter::eNearest,
      vk::ImageAspectFlagBits::eColor, true);
  command_buffer->TransitionImageLayout(tmp_image, vk::ImageLayout::eUndefined,
                                        vk::ImageLayout::eGeneral);

  if (!sampling_filtering_packed_kernel_) {
    FTL_DLOG(INFO) << "Lazily instantiating sampling_filtering_packed_kernel_";
    sampling_filtering_packed_kernel_ = std::make_unique<ComputeShader>(
        vulkan_context(),
        std::vector<vk::ImageLayout>{vk::ImageLayout::eShaderReadOnlyOptimal,
                                     vk::ImageLayout::eGeneral},
        0, g_sampling_filtering_packed_kernel_src, compiler_);
  }

  sampling_filtering_packed_kernel_->Dispatch({depth_texture, tmp_texture},
                                              command_buffer, work_groups_x,
                                              work_groups_y, 1, nullptr);

  timestamper->AddTimestamp(
      "generated spiffy new SSDO acceleration lookup table");
  return tmp_texture;
}

TexturePtr SsdoAccelerator::GenerateHighLowLookupTable(
    CommandBuffer* command_buffer,
    const TexturePtr& depth_texture,
    vk::ImageUsageFlags image_flags,
    Timestamper* timestamper) {
  uint32_t width = depth_texture->width();
  uint32_t height = depth_texture->height();

  // Set below.
  ComputeShader* high_low_neighbors_kernel = nullptr;
  uint32_t work_groups_x = 0;
  uint32_t work_groups_y = 0;
  uint32_t packed_width = 0;
  uint32_t packed_height = 0;

  if (mode_ == 2) {
    // The "sequential kernel" processes a kSize x kSize region in a single
    // thread.  No memory is shared with other threads in the workgroup.
    // TODO: it seems like the parallel kernel should outperform this one, but
    // it doesn't.  We need to understand why this is.  Are we
    // bandwidth-limited?
    // That doesn't explain the significant slowdown.
    if (!high_low_neighbors_packed_kernel_) {
      FTL_DLOG(INFO)
          << "Lazily instantiating high_low_neighbors_packed_kernel_";
      high_low_neighbors_packed_kernel_ = std::make_unique<ComputeShader>(
          vulkan_context(),
          std::vector<vk::ImageLayout>{vk::ImageLayout::eShaderReadOnlyOptimal,
                                       vk::ImageLayout::eGeneral},
          0, g_high_low_neighbors_packed_kernel_src, compiler_);
    }
    high_low_neighbors_kernel = high_low_neighbors_packed_kernel_.get();
    // Size of neighborhood of pixels to work on for each invocation of the
    // compute kernel.  Must match the value in the compute shader source code,
    // and be a multiple of 4.
    constexpr uint32_t kSize = 8;
    work_groups_x = width / kSize + (width % kSize > 0 ? 1 : 0);
    work_groups_y = height / kSize + (height % kSize > 0 ? 1 : 0);
    packed_width = width / 4 + (width % kSize > 0 ? 1 : 0);
    packed_height = height / 4 + (height % kSize > 0 ? 1 : 0);
  } else if (mode_ == 3) {
    // The "parallel" kernel processes a 4x4 region with each thread, and shares
    // memory between all threads in the workgroup.
    if (!high_low_neighbors_packed_parallel_kernel_) {
      FTL_DLOG(INFO)
          << "Lazily instantiating high_low_neighbors_packed_parallel_kernel_";
      high_low_neighbors_packed_parallel_kernel_ =
          std::make_unique<ComputeShader>(
              vulkan_context(),
              std::vector<vk::ImageLayout>{
                  vk::ImageLayout::eShaderReadOnlyOptimal,
                  vk::ImageLayout::eGeneral},
              0, g_high_low_neighbors_packed_parallel_kernel_src, compiler_);
    }
    high_low_neighbors_kernel =
        high_low_neighbors_packed_parallel_kernel_.get();
    constexpr uint32_t kSize = 4;
    work_groups_x = width / kSize + (width % kSize > 0 ? 1 : 0);
    work_groups_y = height / kSize + (height % kSize > 0 ? 1 : 0);
    packed_width = width / 4 + (width % kSize > 0 ? 1 : 0);
    packed_height = height / 4 + (height % kSize > 0 ? 1 : 0);
  } else {
    FTL_CHECK(false);  // unexpected mode_
  }

  ImagePtr tmp_image = image_cache_->NewImage(
      {vk::Format::eR8G8B8A8Unorm, packed_width, packed_height, 1,
       image_flags | vk::ImageUsageFlagBits::eStorage});
  TexturePtr tmp_texture = ftl::MakeRefCounted<Texture>(
      life_preserver_, tmp_image, vk::Filter::eNearest,
      vk::ImageAspectFlagBits::eColor, true);
  command_buffer->TransitionImageLayout(tmp_image, vk::ImageLayout::eUndefined,
                                        vk::ImageLayout::eGeneral);

  high_low_neighbors_kernel->Dispatch({depth_texture, tmp_texture},
                                      command_buffer, work_groups_x,
                                      work_groups_y, 1, nullptr);

  if (high_low_neighbors_kernel == high_low_neighbors_packed_kernel_.get()) {
    timestamper->AddTimestamp(
        "generated SSDO acceleration high/low neighbors lookup table");
  } else {
    timestamper->AddTimestamp(
        "generated SSDO acceleration high/low neighbors lookup table "
        "(parallel)");
  }
  return tmp_texture;
}

TexturePtr SsdoAccelerator::GenerateNullLookupTable(
    CommandBuffer* command_buffer,
    const TexturePtr& depth_texture,
    vk::ImageUsageFlags image_flags,
    Timestamper* timestamper) {
  uint32_t width = depth_texture->width();
  uint32_t height = depth_texture->height();

  // Size of neighborhood of pixels to work on for each invocation of the
  // compute kernel.  Must match the value in the compute shader source code,
  // and be a multiple of 4.
  constexpr uint32_t kSize = 8;

  uint32_t work_groups_x = width / kSize + (width % kSize > 0 ? 1 : 0);
  uint32_t work_groups_y = height / kSize + (height % kSize > 0 ? 1 : 0);

  uint32_t packed_width = width / 4 + (width % kSize > 0 ? 1 : 0);
  uint32_t packed_height = height / 4 + (height % kSize > 0 ? 1 : 0);

  ImagePtr tmp_image = image_cache_->NewImage(
      {vk::Format::eR8G8B8A8Unorm, packed_width, packed_height, 1,
       image_flags | vk::ImageUsageFlagBits::eStorage});
  TexturePtr tmp_texture = ftl::MakeRefCounted<Texture>(
      life_preserver_, tmp_image, vk::Filter::eNearest,
      vk::ImageAspectFlagBits::eColor, true);
  command_buffer->TransitionImageLayout(tmp_image, vk::ImageLayout::eUndefined,
                                        vk::ImageLayout::eGeneral);

  if (!null_packed_kernel_) {
    FTL_DLOG(INFO) << "Lazily instantiating null_packed_kernel_";
    null_packed_kernel_ = std::make_unique<ComputeShader>(
        vulkan_context(),
        std::vector<vk::ImageLayout>{vk::ImageLayout::eShaderReadOnlyOptimal,
                                     vk::ImageLayout::eGeneral},
        0, g_null_packed_kernel_src, compiler_);
  }

  null_packed_kernel_->Dispatch({depth_texture, tmp_texture}, command_buffer,
                                work_groups_x, work_groups_y, 1, nullptr);

  timestamper->AddTimestamp("generated null SSDO acceleration lookup table");
  return tmp_texture;
}

TexturePtr SsdoAccelerator::UnpackLookupTable(
    CommandBuffer* command_buffer,
    const TexturePtr& packed_lookup_table,
    uint32_t width,
    uint32_t height,
    Timestamper* timestamper) {
  constexpr uint32_t kSize = 8;
  FTL_DCHECK(width <= packed_lookup_table->width() * 4);
  FTL_DCHECK(height <= packed_lookup_table->height() * 4);
  FTL_DCHECK(width + kSize > packed_lookup_table->width() * 4);
  FTL_DCHECK(height + kSize > packed_lookup_table->height() * 4);

  ImagePtr result_image =
      image_cache_->NewImage({vk::Format::eR8G8B8A8Unorm, width, height, 1,
                              vk::ImageUsageFlagBits::eStorage |
                                  vk::ImageUsageFlagBits::eTransferSrc});
  command_buffer->TransitionImageLayout(
      result_image, vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral);
  TexturePtr result_texture = ftl::MakeRefCounted<Texture>(
      life_preserver_, result_image, vk::Filter::eNearest,
      vk::ImageAspectFlagBits::eColor);

  uint32_t work_groups_x = width / kSize + (width % kSize > 0 ? 1 : 0);
  uint32_t work_groups_y = height / kSize + (height % kSize > 0 ? 1 : 0);

  if (!unpack_32_to_2_kernel_) {
    FTL_DLOG(INFO) << "Lazily instantiating unpack_32_to_2_kernel_";
    unpack_32_to_2_kernel_ = std::make_unique<ComputeShader>(
        vulkan_context(),
        std::vector<vk::ImageLayout>{vk::ImageLayout::eGeneral,
                                     vk::ImageLayout::eGeneral},
        0, g_unpack_32_to_2_kernel_src, compiler_);
  }
  unpack_32_to_2_kernel_->Dispatch({packed_lookup_table, result_texture},
                                   command_buffer, work_groups_x, work_groups_y,
                                   1, nullptr);

  timestamper->AddTimestamp(
      "finished unpacking SSDO acceleration table for debug visualization");

  return result_texture;
}

void SsdoAccelerator::CycleMode() {
  mode_ = (mode_ + 1) % 4;
  switch (mode_) {
    case 0:
      FTL_LOG(INFO) << "SsdoAccelerator mode: null";
      break;
    case 1:
      FTL_LOG(INFO) << "SsdoAccelerator mode: directional casters";
      break;
    case 2:
      FTL_LOG(INFO) << "SsdoAccelerator mode: high/low neighbors";
      break;
    case 3:
      FTL_LOG(INFO) << "SsdoAccelerator mode: high/low neighbors (parallel)";
      break;
    default:
      FTL_CHECK(false);
  }
}

}  // namespace impl
}  // namespace escher
