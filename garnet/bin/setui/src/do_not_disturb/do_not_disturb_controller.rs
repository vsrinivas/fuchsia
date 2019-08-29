use {
    crate::registry::base::Command,
    crate::registry::service_context::ServiceContext,
    failure::format_err,
    fuchsia_async as fasync,
    futures::StreamExt,
    std::sync::{Arc, RwLock},
};

pub fn spawn_do_not_disturb_controller(
    _service_context_handle: Arc<RwLock<ServiceContext>>,
) -> futures::channel::mpsc::UnboundedSender<Command> {
    let (do_not_disturb_handler_tx, mut do_not_disturb_handler_rx) =
        futures::channel::mpsc::unbounded::<Command>();

    fasync::spawn(async move {
        while let Some(command) = do_not_disturb_handler_rx.next().await {
            match command {
                Command::ChangeState(_state) => {
                    // TODO (go/fxb/25472): Implement this
                }
                Command::HandleRequest(_request, responder) => {
                    // TODO (go/fxb/25472): Implement this
                    responder.send(Err(format_err!("HandleRequest not yet supported"))).ok();
                }
            }
        }
    });
    do_not_disturb_handler_tx
}
