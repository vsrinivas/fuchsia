// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::scanout::Scanout,
    anyhow::Error,
    fidl_fuchsia_ui_composition::LayoutInfo,
    futures::{
        channel::{mpsc, oneshot},
        SinkExt,
    },
};

#[derive(Clone)]
pub struct ScanoutId {
    pub index: usize,
    pub generation: usize,
}

pub type AttachScanoutResponder = oneshot::Sender<Result<ScanoutId, Error>>;

pub enum GpuCommand {
    AttachScanout { scanout: Box<dyn Scanout>, responder: AttachScanoutResponder },
    ResizeScanout { scanout_id: ScanoutId, layout_info: LayoutInfo },
    DetachScanout { scanout_id: ScanoutId },
}

#[derive(Clone)]
pub struct GpuCommandSender {
    sender: mpsc::Sender<GpuCommand>,
}

impl GpuCommandSender {
    pub fn new(sender: mpsc::Sender<GpuCommand>) -> Self {
        Self { sender }
    }

    pub async fn attach_scanout(
        &mut self,
        scanout: Box<dyn Scanout>,
    ) -> Result<ScanoutController, Error> {
        let (response_sender, response_receiver) = oneshot::channel();
        let command = GpuCommand::AttachScanout { scanout, responder: response_sender };
        if let Err(e) = self.sender.try_send(command) {
            self.sender.send(e.into_inner()).await?;
        }
        let scanout_id = response_receiver.await??;
        Ok(ScanoutController { sender: self.sender.clone(), scanout_id })
    }
}

pub struct ScanoutController {
    sender: mpsc::Sender<GpuCommand>,
    scanout_id: ScanoutId,
}

impl ScanoutController {
    pub async fn resize(&mut self, layout_info: LayoutInfo) -> Result<(), Error> {
        self.sender
            .send(GpuCommand::ResizeScanout { scanout_id: self.scanout_id.clone(), layout_info })
            .await?;
        Ok(())
    }

    pub async fn detach(mut self) -> Result<(), Error> {
        self.sender.send(GpuCommand::DetachScanout { scanout_id: self.scanout_id.clone() }).await?;
        Ok(())
    }
}
