// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_FEATURES_H_
#define GPU_FEATURES_H_

#include "registers.h"
#include <vector>

class GpuFeatures {
public:
    GpuFeatures(magma::RegisterIo* io)
    {
        revision_ = registers::Revision::Get().ReadFrom(io);
        features_ = registers::Features::Get().ReadFrom(io);

        minor_features_.resize(6);
        minor_features_[0] = registers::MinorFeatures::Get(0).ReadFrom(io);
        if (minor_features_[0].reg_value() & registers::MinorFeatures::kMoreMinorFeatures) {
            for (uint32_t i = 1; i < minor_features_.size(); i++) {
                minor_features_[i] = registers::MinorFeatures::Get(i).ReadFrom(io);
            }
        }

        specs1_ = registers::Specs1::Get().ReadFrom(io);
        specs2_ = registers::Specs2::Get().ReadFrom(io);
        specs3_ = registers::Specs3::Get().ReadFrom(io);
        specs4_ = registers::Specs4::Get().ReadFrom(io);
    }

    uint32_t revision() { return revision_.reg_value(); }
    registers::Features& features() { return features_; }

    uint32_t minor_features(uint32_t index) { return minor_features_[index].reg_value(); }
    bool halti5() { return minor_features_[5].reg_value() & registers::MinorFeatures::kHalti5; }
    bool has_mmu() { return minor_features_[1].reg_value() & registers::MinorFeatures::kHasMmu; }

    uint32_t register_max() { return 1u << specs1_.log2_register_max().get(); }
    uint32_t thread_count() { return 1u << specs1_.log2_thread_count().get(); }
    uint32_t vertex_output_buffer_size()
    {
        return 1u << specs1_.log2_vertex_output_buffer_size().get();
    }
    uint32_t vertex_cache_size() { return specs1_.vertex_cache_size().get(); }
    uint32_t shader_core_count() { return specs1_.shader_core_count().get(); }
    uint32_t pixel_pipes() { return specs1_.pixel_pipes().get(); }
    uint32_t stream_count() { return specs4_.stream_count().get(); }
    uint32_t buffer_size() { return specs2_.buffer_size().get(); }
    uint32_t num_constants() { return specs2_.num_constants().get(); }
    uint32_t varyings_count() { return specs3_.varyings_count().get(); }

    uint32_t instruction_count()
    {
        DASSERT(specs2_.instruction_count().get() == 0);
        return 256;
    }

private:
    registers::Revision revision_;
    registers::Features features_;
    std::vector<registers::MinorFeatures> minor_features_;
    registers::Specs1 specs1_;
    registers::Specs2 specs2_;
    registers::Specs3 specs3_;
    registers::Specs4 specs4_;
};

#endif // GPU_FEATURES_H_
