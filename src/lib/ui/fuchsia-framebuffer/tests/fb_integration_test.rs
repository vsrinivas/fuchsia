// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error};
use fuchsia_async::{self as fasync, DurationExt, Timer};
use fuchsia_framebuffer::{
    FrameBuffer, FrameCollectionBuilder, FrameUsage, ImageId, ImageInCollection, Message,
    PixelFormat,
};
use fuchsia_zircon::DurationNum;
use futures::{channel::mpsc::unbounded, StreamExt};
use std::{cell::RefCell, rc::Rc};

const BUFFER_COUNT: usize = 2;
const BUFFER_COLLECTION_ID: u64 = 1;

fn to_565(pixel: &[u8; 4]) -> [u8; 2] {
    let red = pixel[0] >> 3;
    let green = pixel[1] >> 2;
    let blue = pixel[2] >> 3;
    let b1 = (red << 3) | ((green & 0b11_1000) >> 3);
    let b2 = ((green & 0b111) << 5) | blue;
    [b2, b1]
}

#[derive(Debug, PartialEq)]
enum TestResult {
    TimeoutFired,
    TestPassed,
}

fn test_main() -> Result<(), Error> {
    println!("fuchsia_framebuffer_integration_test");

    let mut executor = fasync::LocalExecutor::new().context("Failed to create executor")?;

    executor.run_singlethreaded(async {
        let (test_sender, mut test_receiver) = unbounded::<TestResult>();
        let timeout_sender = test_sender.clone();

        let (sender, mut receiver) = unbounded::<Message>();

        let mut fb = FrameBuffer::new(FrameUsage::Cpu, None, None, Some(sender))
            .await
            .context("Failed to create framebuffer, perhaps a root presenter is already running")?;

        let config = fb.get_config();
        let frame_collection_builder = FrameCollectionBuilder::new(
            config.width,
            config.height,
            config.format.into(),
            FrameUsage::Cpu,
            BUFFER_COUNT,
        )
        .expect("frame_collection_builder");
        let config = fb.get_config();
        let mut frame_collection =
            frame_collection_builder.build(BUFFER_COLLECTION_ID, &config, true, &mut fb).await?;

        let image_ids: Vec<ImageId> = frame_collection.get_image_ids().into_iter().collect();
        let (image_sender, mut image_receiver) = unbounded::<ImageInCollection>();

        let image_id_1 = image_ids[0];
        let frame = frame_collection.get_frame_mut(image_id_1);

        let grey = [0x80, 0x80, 0x80, 0xFF];
        if config.format != PixelFormat::Rgb565 {
            frame.fill_rectangle(0, 0, config.width, config.height, &grey);
        } else {
            frame.fill_rectangle(0, 0, config.width, config.height, &to_565(&grey));
        }

        frame.flush()?;
        fb.present_frame(frame, Some(image_sender), true)?;

        let image_id_2 = image_ids[1];

        let fb_ptr = Rc::new(RefCell::new(fb));

        // Listen for vsync messages to schedule an update of the displayed image
        fasync::Task::local(async move {
            receiver.next().await;
            let mut fb = fb_ptr.borrow_mut();
            let frame2 = frame_collection.get_frame_mut(image_id_2);
            let white = [0x0ff, 0xff, 0xff, 0xFF];
            if config.format != PixelFormat::Rgb565 {
                frame2.fill_rectangle(0, 0, config.width, config.height, &white);
            } else {
                frame2.fill_rectangle(0, 0, config.width, config.height, &to_565(&white));
            }
            frame2.flush().expect("flush");
            fb.present_frame(frame2, None, true).expect("frame2 present to succeed");
            image_receiver.next().await;
            test_sender.unbounded_send(TestResult::TestPassed).unwrap();
        })
        .detach();

        let timeout = Timer::new(5_i64.second().after_now());
        fasync::Task::local(async move {
            timeout.await;
            timeout_sender
                .unbounded_send(TestResult::TimeoutFired)
                .expect("test_sender.send expected to work");
        })
        .detach();

        let r = test_receiver.next().await;
        assert_eq!(r.unwrap(), TestResult::TestPassed);
        Ok::<(), Error>(())
    })?;
    Ok(())
}

fn main() -> Result<(), Error> {
    test_main()
}

#[cfg(test)]
mod test {
    use crate::test_main;

    #[test]
    fn fb_integration_test() -> std::result::Result<(), anyhow::Error> {
        test_main()
    }
}
