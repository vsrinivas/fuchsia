use fuchsia_syslog::fx_log_warn;

// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// This macro generates a mod containing the logic to process a FIDL stream for the
/// fuchsia.settings namespace.
/// Callers can spawn a handler task by invoking fidl_io::spawn.
/// Macro usages specify the interface's base name (prefix for all generated
/// classes), along with a repeated set of the following:
/// - `Switchboard` setting type.
/// - FIDL setting type.
/// - The responder type for the `HangingGetHandler`.
/// - A type of a key for change functions if custom change functions are used.
///     - If change functions aren't used, the `change_func_key` is ignored,
///       so can be anything.
///       TODO(fxb/68167): Restructure this code to avoid needing to specify
///       unneeded parameters.
/// - The handler function for requests.
#[macro_export]
macro_rules! fidl_process_full {
    ($interface:ident $(,$setting_type:expr, $fidl_settings:ty,
            $fidl_responder:ty, $change_func_key:ty, $handle_func:ident)+$(,)*) => {
        type HandleResult<'a> = ::futures::future::LocalBoxFuture<
            'a,
            Result<Option<paste::paste!{[<$interface Request>]}>, anyhow::Error>,
        >;

        pub(crate) mod fidl_io {
            paste::paste!{
                use fidl_fuchsia_settings::{[<$interface Marker>], [<$interface RequestStream>]};
            }
            use super::*;
            use $crate::fidl_processor::processor::SettingsFidlProcessor;
            use $crate::service;
            use $crate::message::base::MessengerType;
            use ::fuchsia_async as fasync;
            use ::futures::FutureExt;

            pub(crate) fn spawn (
                delegate: service::message::Delegate,
                stream: paste::paste!{[<$interface RequestStream>]}
            ) {
                fasync::Task::local(async move {
                    let service_messenger = delegate
                        .create(MessengerType::Unbound)
                        .await.expect("service messenger should be created")
                        .0;

                    let mut processor =
                        SettingsFidlProcessor::<paste::paste!{[<$interface Marker>]}>::new(
                            stream, service_messenger,
                        )
                        .await;
                    $(
                        processor
                            .register::<$fidl_settings, $fidl_responder, $change_func_key>(
                                $setting_type,
                                Box::new(move |context, req| -> HandleResult<'_> {
                                    async move { $handle_func(context, req).await }.boxed_local()
                                }),
                            )
                            .await;
                    )*
                    processor.process().await;
                }).detach();
            }
        }
    };
}

#[macro_export]
macro_rules! fidl_process {
    // Generates a fidl_io mod with a spawn for the given fidl interface,
    // setting type, and handler function. Additional handlers can be specified
    // by providing the internal setting type, fidl setting type, watch
    // responder, and handle function.
    ($interface:ident, $setting_type:expr, $handle_func:ident
            $(,$item_setting_type:expr, $fidl_settings:ty, $fidl_responder:ty,
            $item_handle_func:ident)*$(,)*) => {
        paste::paste! {
            $crate::fidl_process_full!(
                $interface,
                $setting_type,
                [<$interface Settings>],
                [<$interface WatchResponder>],
                String,
                $handle_func
                $(,$item_setting_type, $fidl_settings, $fidl_responder, String, $item_handle_func)*
            );
        }
    };

    // Generates a fidl_io mod with a spawn for the given fidl interface,
    // setting type, and handler function. Additional handlers can be specified
    // by providing the responder type and handle function.
    ($interface:ident, $setting_type:expr, $handle_func:ident
            $(, $fidl_responder:ty, $item_handle_func:ident)*$(,)*) => {
        paste::paste! {
            $crate::fidl_process_full!(
                $interface,
                $setting_type,
                [<$interface Settings>],
                [<$interface WatchResponder>],
                String,
                $handle_func
                $(, $setting_type,
                [<$interface Settings>],
                $fidl_responder,
                String,
                $item_handle_func)*
            );
        }
    };

    // Generates a fidl_io mod with a spawn for the given fidl interface,
    // setting type, fidl setting type and handler function. To be used when the
    // fidl interface and fidl setting type differ in name.
    ($interface:ident, $setting_type:expr, $fidl_settings:ident,
            $handle_func:ident) => {
        paste::paste! {
            $crate::fidl_process_full!(
                $interface,
                $setting_type,
                $fidl_settings,
                [<$interface WatchResponder>],
                String,
                $handle_func
            );
        }
    };
}

/// Shuts down the given fidl `responder` using a zircon epitaph generated from
/// the given `error`.
#[macro_export]
macro_rules! shutdown_responder_with_error {
    ($responder:expr, $error:ident) => {
        // Extra pair of braces is needed to limit the scope of the import.
        {
            use ::fidl::prelude::*;
            $responder
                .control_handle()
                .shutdown_with_epitaph(crate::fidl_common::convert_to_epitaph($error))
        }
    };
}

/// Custom trait used to handle results from responding to FIDL calls.
pub trait FidlResponseErrorLogger {
    fn log_fidl_response_error(&self, client_name: &str);
}

/// In order to not crash when a client dies, logs but doesn't crash for the specific case of
/// being unable to write to the client. Crashes if other types of errors occur.
impl FidlResponseErrorLogger for Result<(), fidl::Error> {
    fn log_fidl_response_error(&self, client_name: &str) {
        if let Some(error) = self.as_ref().err() {
            match error {
                fidl::Error::ServerResponseWrite(_) => {
                    fx_log_warn!("Failed to respond to client {:?} : {:?}", client_name, error);
                }
                _ => {
                    panic!(
                        "Unexpected client response error from client {:?} : {:?}",
                        client_name, error
                    );
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use fuchsia_zircon as zx;

    use super::*;

    // Should succeed either when responding was successful or there was an error on the client
    // side.
    #[test]
    fn test_error_logger_succeeds() {
        let result = Err(fidl::Error::ServerResponseWrite(zx::Status::PEER_CLOSED));
        result.log_fidl_response_error("");

        let result = Ok(());
        result.log_fidl_response_error("");
    }

    // Should fail at all other times.
    #[should_panic]
    #[test]
    fn test_error_logger_fails() {
        let result = Err(fidl::Error::ServerRequestRead(zx::Status::PEER_CLOSED));
        result.log_fidl_response_error("");
    }
}
