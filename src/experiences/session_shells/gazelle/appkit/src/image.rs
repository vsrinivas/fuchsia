// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Error, fidl_fuchsia_math as fmath, fidl_fuchsia_ui_composition as ui_comp};

use crate::utils::ImageData;

pub struct Image {
    flatland: ui_comp::FlatlandProxy,
    content_id: ui_comp::ContentId,
}

impl Image {
    pub(crate) fn new(
        image_data: &mut ImageData,
        flatland: ui_comp::FlatlandProxy,
        mut content_id: ui_comp::ContentId,
    ) -> Result<Image, Error> {
        flatland.create_image(
            &mut content_id,
            &mut image_data.import_token,
            image_data.vmo_index,
            ui_comp::ImageProperties {
                size: Some(fmath::SizeU { width: image_data.width, height: image_data.height }),
                ..ui_comp::ImageProperties::EMPTY
            },
        )?;

        Ok(Image { flatland: flatland.clone(), content_id })
    }

    pub fn get_content_id(&self) -> ui_comp::ContentId {
        self.content_id.clone()
    }

    pub fn set_size(&self, width: u32, height: u32) -> Result<(), Error> {
        let mut content_id = self.get_content_id();
        let mut size = fmath::SizeU { width, height };
        self.flatland.set_image_destination_size(&mut content_id, &mut size)?;
        Ok(())
    }
}
