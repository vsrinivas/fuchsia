use {
    crate::registry::base::Command, failure::format_err, fuchsia_async as fasync,
    futures::StreamExt,
};

pub fn spawn_device_controller() -> futures::channel::mpsc::UnboundedSender<Command> {
    let (device_handler_tx, mut device_handler_rx) = futures::channel::mpsc::unbounded::<Command>();

    fasync::spawn(async move {
        while let Some(command) = device_handler_rx.next().await {
            match command {
                Command::ChangeState(_state) => {
                    // TODO (go/fxb/36349): Implement this
                }
                Command::HandleRequest(_request, responder) => {
                    // TODO (go/fxb/36349): Implement this
                    responder.send(Err(format_err!("HandleRequest not yet supported"))).ok();
                }
            }
        }
    });
    device_handler_tx
}
