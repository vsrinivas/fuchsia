// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
mod tests {
    use fidl_fuchsia_update::ChannelControlMarker;
    use fuchsia_async as fasync;
    use fuchsia_component::client::connect_to_service;

    #[fasync::run_singlethreaded(test)]
    async fn test_fake_channel_control() {
        let control = connect_to_service::<ChannelControlMarker>().unwrap();

        control.set_target("test-target-channel").await.unwrap();
        assert_eq!("test-target-channel", control.get_target().await.unwrap());
        assert_eq!("fake-current-channel", control.get_channel().await.unwrap());

        control.set_target("test-target-channel-2").await.unwrap();
        assert_eq!("test-target-channel-2", control.get_target().await.unwrap());
        assert_eq!("fake-current-channel", control.get_channel().await.unwrap());
    }

}
