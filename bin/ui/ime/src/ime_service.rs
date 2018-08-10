use async;
use fidl::endpoints2::RequestStream;
use fidl_fuchsia_ui_input as uii;
use futures::future;
use futures::prelude::*;

use ime::IME;

pub fn spawn_server(chan: async::Channel) {
    let stream_complete = uii::ImeServiceRequestStream::from_channel(chan)
        .try_for_each(move |get_ime_request| {
            let uii::ImeServiceRequest::GetInputMethodEditor {
                keyboard_type,
                action,
                initial_state,
                client,
                editor,
                ..
            } = get_ime_request;

            if let Ok(edit_stream) = editor.into_stream() {
                if let Ok(client_proxy) = client.into_proxy() {
                    IME::new(keyboard_type, action, initial_state, client_proxy).bind(edit_stream);
                }
            }
            future::ready(Ok(()))
        })
        .unwrap_or_else(|e| eprintln!("error running ime server: {:?}", e));
    async::spawn(stream_complete);
}
