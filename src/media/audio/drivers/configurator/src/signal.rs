// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    anyhow::{anyhow, format_err, Error},
    fidl_fuchsia_hardware_audio::*,
    fidl_fuchsia_hardware_audio_signalprocessing::*,
};

pub struct SignalInterface {
    /// The proxy if connected.
    proxy: Option<SignalProcessingProxy>,
}

impl SignalInterface {
    /// A new interface that will connect to the device at the `path` within the `dev_proxy`
    /// directory. The interface is unconnected when created.
    pub fn new() -> Self {
        Self { proxy: None }
    }

    /// Get the signal proxy.
    pub fn get_proxy(&self) -> Result<&SignalProcessingProxy, Error> {
        self.proxy.as_ref().ok_or(format_err!("Proxy not connected"))
    }

    /// Connect a codec to the SignalInterface.
    pub fn connect_codec(&mut self, codec_proxy: &CodecProxy) -> Result<(), Error> {
        let (ours, theirs) = fidl::endpoints::create_proxy::<SignalProcessingMarker>()?;
        codec_proxy.signal_processing_connect(theirs)?;

        self.proxy = Some(ours);
        Ok(())
    }

    /// Get processing elements.
    pub async fn get_elements(&self) -> Result<Vec<Element>, Error> {
        self.get_proxy()?
            .clone()
            .get_elements()
            .await?
            .map_err(|e| anyhow!("Get elements error: {:?}", e))
    }

    /// Set processing element state.
    pub async fn set_element_state(&self, id: u64, state: ElementState) -> Result<(), Error> {
        self.get_proxy()?
            .clone()
            .set_element_state(id, state)
            .await?
            .map_err(|e| anyhow!("Set element error: {:?}", e))
    }

    /// Watch processing element state.
    #[cfg(test)]
    pub async fn watch_element_state(&self, id: u64) -> Result<ElementState, Error> {
        Ok(self.get_proxy()?.clone().watch_element_state(id).await?)
    }
}
