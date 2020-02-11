// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include <soc/aml-common/aml-gpu.h>
#include <soc/aml-s905d2/s905d2-hiu.h>

zx_status_t aml_gp0_init(aml_gpu_t* gpu) {
  gpu->hiu_dev = calloc(1, sizeof(*gpu->hiu_dev));
  gpu->gp0_pll_dev = calloc(1, sizeof(*gpu->gp0_pll_dev));
  // HIU Init.
  zx_status_t status = s905d2_hiu_init(gpu->hiu_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml_gp0_init: hiu_init failed: %d\n", status);
    return status;
  }

  status = s905d2_pll_init(gpu->hiu_dev, gpu->gp0_pll_dev, GP0_PLL);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml_gp0_init: pll_init failed: %d\n", status);
    return status;
  }

  status = s905d2_pll_set_rate(gpu->gp0_pll_dev, 846000000);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml_gp0_init: pll_set_rate failed: %d\n", status);
    return status;
  }
  status = s905d2_pll_ena(gpu->gp0_pll_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml_gp0_init: pll_ena failed: %d\n", status);
    return status;
  }
  return ZX_OK;
}

void aml_gp0_release(aml_gpu_t* gpu) {
  // TODO: turn off PLL
  free(gpu->gp0_pll_dev);
  free(gpu->hiu_dev);
}
