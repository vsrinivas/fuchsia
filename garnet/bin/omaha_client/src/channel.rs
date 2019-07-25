// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
mod tests {
    use fidl_fuchsia_update::{ChannelControlMarker, ChannelWriterMarker, Slot};
    use fuchsia_async as fasync;
    use fuchsia_component::client::connect_to_service;

    #[fasync::run_singlethreaded(test)]
    async fn test_fake_channel_writer() {
        let writer = connect_to_service::<ChannelWriterMarker>().unwrap();

        let data = vec![1, 2, 3];
        let result = await!(writer.set_channel_data(Slot::A, &mut data.iter().cloned())).unwrap();
        assert!(result.is_ok());
        let (slot, actual_data) = await!(writer.get_channel_data()).unwrap();
        assert_eq!(Slot::A, slot);
        assert_eq!(Some(data), actual_data);

        let data_b = vec![4, 5, 6, 7];
        let result = await!(writer.set_channel_data(Slot::B, &mut data_b.iter().cloned())).unwrap();
        assert!(result.is_ok());
        let (slot, actual_data) = await!(writer.get_channel_data()).unwrap();
        assert_eq!(Slot::B, slot);
        assert_eq!(Some(data_b), actual_data);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_fake_channel_control() {
        let control = connect_to_service::<ChannelControlMarker>().unwrap();

        await!(control.set_target("test-target-channel")).unwrap();
        assert_eq!("test-target-channel", await!(control.get_target()).unwrap());
        assert_eq!("fake-current-channel", await!(control.get_channel()).unwrap());

        await!(control.set_target("test-target-channel-2")).unwrap();
        assert_eq!("test-target-channel-2", await!(control.get_target()).unwrap());
        assert_eq!("fake-current-channel", await!(control.get_channel()).unwrap());
    }

}
