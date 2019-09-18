// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    futures::{
        channel::mpsc::{unbounded, TrySendError, UnboundedSender},
        Future, StreamExt,
    },
    std::pin::Pin,
};

/// A struct used to execute Futures in the order they are sent.
///
/// Any cloned `SequentialSender` shares the same underlying channel.
#[derive(Clone)]
pub struct SequentialSender {
    /// The sender which is used to send futures for execution.
    sender: UnboundedSender<Pin<Box<dyn Future<Output = ()>>>>,
}

/// The result type for a call to `SequentialSender.send`. The `TrySendError` will contain the
/// future which failed to be sent on error.
pub type SendResult = Result<(), TrySendError<Pin<Box<dyn Future<Output = ()>>>>>;

impl SequentialSender {
    /// Creates a new `SequentialSender` and a `Future` which drives the execution of sent futures.
    ///
    /// # Example
    ///
    /// ```
    /// let (sender, future) = SequentialSender::new();
    /// spawn_local(future);
    /// sender.send(
    ///     async move {
    ///         println!("Running first future!");
    ///     },
    /// ).expect("Sent the future.");
    /// ```
    ///
    /// # Returns
    /// A new `SequentialSender` and a `Future` which executes the sent futures.
    pub fn new() -> (SequentialSender, impl Future<Output = ()>) {
        let (sender, mut receiver) = unbounded();

        (
            SequentialSender { sender: sender },
            async move {
                while let Some(next_future) = receiver.next().await {
                    next_future.await;
                }
            },
        )
    }

    /// Sends a future for execution.
    ///
    /// All sent futures are executed in the order they were sent.
    ///
    /// # Parameters
    /// - `future`: The future to be queued for execution.
    ///
    /// # Returns
    /// A `SendResult` will contain a `TrySendError` with the future which failed to be sent on
    /// error.
    pub fn send<Fut>(&self, future: Fut) -> SendResult
    where
        Fut: Future<Output = ()> + 'static,
    {
        self.sender.unbounded_send(Pin::from(Box::new(future)))
    }
}

#[cfg(test)]
mod tests {
    use {
        super::SequentialSender,
        fuchsia_async::spawn_local,
        futures::{channel::mpsc::channel, channel::oneshot, task::Poll, SinkExt, StreamExt},
        pin_utils::pin_mut,
    };

    /// Tests that two futures which can be completed immediately are executed in the order they
    /// are sent.
    #[fuchsia_async::run_singlethreaded]
    #[test]
    async fn sequential_immediate() {
        let (sequential_sender, sender_future) = SequentialSender::new();

        // The channel which is used to verify the execution order of the futures.
        let (result_sender, result_receiver) = channel::<&str>(1);

        // The first future will send `first_expected_message` via `first_sender`.
        let mut first_sender = result_sender.clone();
        let first_expected_message = "first";

        // The second future will send `second_expected_message` via `second_sender`.
        let mut second_sender = result_sender.clone();
        let second_expected_message = "second";

        spawn_local(sender_future);

        sequential_sender
            .send(
                async move {
                    first_sender.send(first_expected_message).await
                        .expect("Could not send first completion.");
                },
            )
            .expect("Could not send first future.");

        sequential_sender
            .send(
                async move {
                    second_sender.send(second_expected_message).await
                        .expect("Could not send second completion.");
                },
            )
            .expect("Could not send second future.");

        // Await the completion of the sent futures.
        let (first_message, stream_tail) = result_receiver.into_future().await;
        let (second_message, _) = stream_tail.into_future().await;

        // Assert the messages were received in the correct order.
        assert_eq!(first_message, Some(first_expected_message));
        assert_eq!(second_message, Some(second_expected_message));
    }

    /// Tests that futures are executed in order by delaying the first future's execution until the
    /// second future has been sent successfully.
    #[test]
    fn sequential_delayed() {
        let (sequential_sender, sender_future) = SequentialSender::new();
        pin_mut!(sender_future);

        // This channel is used to delay the completion of the first sent future to make sure it
        // does not complete before the second future has been sent and given a chance to execute
        // (i.e. it helps verify that the second future does not begin executing until the first
        // one is complete).
        let (delay_sender, delay_receiver) = oneshot::channel();

        // The channel which is used to verify the execution order of the futures.
        let (result_sender, result_receiver) = channel::<&str>(1);

        // The first future will send `first_future_started_message` when it begins executing, and
        // `first_future_completed_message` when it completes, via `first_sender`.
        let mut first_sender = result_sender.clone();
        let first_future_started_message = "first";
        let first_future_completed_message = "first_complete";

        // The second future will send `second_future_completed_message` via `second_sender` when
        // it has executed.
        let mut second_sender = result_sender.clone();
        let second_future_completed_message = "second";

        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");

        // Send both the futures in order, and wait for them to have been sent successfully.
        sequential_sender
            .send(
                async move {
                    first_sender.send(first_future_started_message).await
                        .expect("Could not send first started.");
                    delay_receiver.await.unwrap();
                    first_sender.send(first_future_completed_message).await
                        .expect("Could not send first completion.");
                },
            )
            .expect("Could not send first future.");

        sequential_sender
            .send(
                async move {
                    second_sender.send(second_future_completed_message).await
                        .expect("Could not send second completion.");
                },
            )
            .expect("Could not send second future.");

        assert_eq!(exec.run_until_stalled(&mut sender_future), Poll::Pending);

        // Capture the first message, which should be the first future indicating that it began
        // executing.
        let (first_message, result_receiver) =
            exec.run_singlethreaded(result_receiver.into_future());

        assert_eq!(exec.run_until_stalled(&mut sender_future), Poll::Pending);

        // Allow the first future to continue executing.
        delay_sender.send(()).unwrap();

        assert_eq!(exec.run_until_stalled(&mut sender_future), Poll::Pending);

        // Await the sent futures having completed execution.
        let (second_message, result_receiver) =
            exec.run_singlethreaded(result_receiver.into_future());
        let (third_message, _) = exec.run_singlethreaded(result_receiver.into_future());

        // Assert the messages were received in the correct order.
        assert_eq!(first_message, Some(first_future_started_message));
        assert_eq!(second_message, Some(first_future_completed_message));
        assert_eq!(third_message, Some(second_future_completed_message));
    }

    /// Tests that futures are executed in order when sent on a cloned sender.
    #[test]
    fn sequential_cloned() {
        let (sequential_sender, sender_future) = SequentialSender::new();
        let second_sequential_sender = sequential_sender.clone();
        pin_mut!(sender_future);

        // This channel is used to delay the execution of the first future which is sent, to make
        // sure it does not execute prior to the second future being sent.
        let (delay_sender, delay_receiver) = oneshot::channel();

        // The channel which is used to verify the execution order of the futures.
        let (result_sender, result_receiver) = channel::<&str>(1);

        // The first future will send `first_expected_message` via `first_sender`.
        let mut first_sender = result_sender.clone();
        let first_future_started_message = "first";
        let first_future_completed_message = "first_complete";

        // The second future will send `second_expected_message` via `second_sender`.
        let mut second_sender = result_sender.clone();
        let second_future_completed_message = "second";

        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");

        // Send both the futures in order, and wait for them to have been sent successfully.
        sequential_sender
            .send(
                async move {
                    first_sender.send(first_future_started_message).await
                        .expect("Could not send first started.");
                    delay_receiver.await.unwrap();
                    first_sender.send(first_future_completed_message).await
                        .expect("Could not send first completion.");
                },
            )
            .expect("Could not send first future.");

        second_sequential_sender
            .send(
                async move {
                    second_sender.send(second_future_completed_message).await
                        .expect("Could not send second completion.");
                },
            )
            .expect("Could not send second future.");

        assert_eq!(exec.run_until_stalled(&mut sender_future), Poll::Pending);

        // Capture the first message, which should be the first future indicating that it began
        // executing.
        let (first_message, result_receiver) =
            exec.run_singlethreaded(result_receiver.into_future());

        assert_eq!(exec.run_until_stalled(&mut sender_future), Poll::Pending);

        // Let the first future complete execution.
        delay_sender.send(()).unwrap();

        assert_eq!(exec.run_until_stalled(&mut sender_future), Poll::Pending);

        // Await the sent futures having completed execution.
        let (second_message, result_receiver) =
            exec.run_singlethreaded(result_receiver.into_future());
        let (third_message, _) = exec.run_singlethreaded(result_receiver.into_future());

        // Assert the messages were received in the correct order.
        assert_eq!(first_message, Some(first_future_started_message));
        assert_eq!(second_message, Some(first_future_completed_message));
        assert_eq!(third_message, Some(second_future_completed_message));
    }
}
