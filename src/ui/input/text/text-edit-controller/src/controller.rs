// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code, unused_imports, unused_variables)]

use ftext::TextEditServerSessionProxyInterface;

use {
    anyhow::{format_err, Error},
    fidl_fuchsia_input_text::{
        self as ftext, CompositionUpdate as FidlCompositionUpdate, TextEditServer_Proxy,
        TextFieldRequest,
    },
    fidl_fuchsia_input_text_ext::IntoRange,
    fuchsia_async as fasync,
    futures::{channel::mpsc, lock::Mutex, prelude::*, FutureExt},
    std::{convert::TryInto, ops::Range, sync::Arc},
    text_edit_model::{CompositionUpdate, TextEditModel, TextFieldError},
};

/// Manages interactions with the TextEditServer; owns the model, view model, and view.
///
/// NOTE: The view model and view code has been removed pending a complete rewrite of the
/// integration with Carnelian.
#[derive(Debug, Clone)]
pub struct TextEditController {
    inner: Arc<Mutex<Inner>>,
}

#[derive(Debug)]
struct Inner {
    id: String,
    model: TextEditModel,
    session: Option<ftext::TextEditServerSessionProxy>,
}

impl TextEditController {
    pub async fn new(
        id: impl Into<String>,
        field_options: ftext::TextFieldOptions,
    ) -> Result<Self, Error> {
        let model = TextEditModel::new(field_options)?;
        let controller = TextEditController {
            inner: Arc::new(Mutex::new(Inner { id: id.into(), model, session: None })),
        };
        Ok(controller)
    }

    async fn connect_to_text_edit_server(
        self,
        text_edit_server: TextEditServer_Proxy,
    ) -> Result<(), Error> {
        // The text field's client is the Text Edit Server.
        // The host is the frontend app that displays a text box.
        let (field_client_end, _field_host_end) =
            fidl::endpoints::create_endpoints::<ftext::TextFieldMarker>()?;
        let field_client_end = field_client_end.into_channel().into();

        let (session_client_end, session_server_end) =
            fidl::endpoints::create_endpoints::<ftext::TextEditServerSessionMarker>()?;

        let options = ftext::TextFieldOptions::EMPTY;

        let id = self.inner.lock().await.id.clone();
        tracing::info!(%id, "Registering text field");
        let _ = text_edit_server
            .register_focused_text_field(field_client_end, session_server_end, options)
            .await?;
        tracing::info!(%id, "Registered text field");

        self.inner.lock().await.session = Some(session_client_end.into_proxy()?);
        let self_ = self.clone();

        let field_host_stream = _field_host_end.into_stream()?;
        let field_handler_future = field_host_stream
            .try_for_each(move |req| {
                tracing::info!("TextFieldRequest: {}", req.method_name());
                let self_ = self_.clone();

                async move {
                    use TextFieldRequest::*;

                    match req {
                        GetText { range, responder } => {
                            let mut result =
                                self_.clone().get_text(&range).await.map_err(Into::into);
                            responder.send(&mut result)?;
                        }
                        SetText { transaction_id, old_range, new_text, responder } => {
                            let mut result = self_
                                .clone()
                                .set_text(&transaction_id, &old_range, new_text)
                                .await
                                .map_err(Into::into);
                            responder.send(&mut result)?;
                        }
                        SetSelection { transaction_id, selection, responder } => {
                            let mut result = self_
                                .clone()
                                .set_selection(&transaction_id, selection)
                                .await
                                .map_err(Into::into);
                            responder.send(&mut result)?;
                        }
                        BeginTransaction { revision_id, responder } => {
                            let mut result = self_
                                .clone()
                                .begin_transaction(&revision_id)
                                .await
                                .map_err(Into::into);
                            responder.send(&mut result)?;
                        }
                        CommitTransaction { transaction_id, responder } => {
                            let mut result = self_
                                .clone()
                                .commit_transaction(&transaction_id)
                                .await
                                .map_err(Into::into);
                            responder.send(&mut result)?;
                        }
                        CommitTransactionInComposition {
                            transaction_id,
                            ctic_options,
                            responder,
                        } => {
                            let mut result = self_
                                .clone()
                                .commit_transaction_in_composition(
                                    transaction_id,
                                    ctic_options.composition_update,
                                )
                                .await
                                .map_err(Into::into);
                            responder.send(&mut result)?;
                        }
                        CancelTransaction { transaction_id, responder } => {
                            let mut result = self_
                                .clone()
                                .cancel_transaction(&transaction_id)
                                .await
                                .map_err(Into::into);
                            responder.send(&mut result)?;
                        }
                        BeginComposition { revision_id, responder, .. } => {
                            let mut result = self_
                                .clone()
                                .begin_composition(&revision_id)
                                .await
                                .map_err(Into::into);
                            responder.send(&mut result)?;
                        }
                        CompleteComposition { responder } => {
                            let mut result =
                                self_.clone().complete_composition().await.map_err(Into::into);
                            responder.send(&mut result)?;
                        }
                        CancelComposition { responder } => {
                            let mut result =
                                self_.clone().cancel_composition().await.map_err(Into::into);
                            responder.send(&mut result)?;
                        }
                    }
                    Ok(())
                }
            })
            .map(|result| {
                if let Err(e) = result {
                    tracing::error!("Controller loop failed with error: {:?}", e);
                }
            });
        fasync::Task::local(field_handler_future).detach();

        // Notify the server of the field's initial state
        self.notify_state_changed().await?;

        Ok(())
    }

    async fn notify_state_changed(self) -> Result<(), Error> {
        let inner = self.inner.lock().await;
        let state = inner.model.state();
        let session = inner.session.clone().expect("session exists");
        session.notify_state_changed(state).await?.to_anyhow_error()?;
        Ok(())
    }

    async fn get_text(self, range: &ftext::Range) -> Result<String, TextFieldError> {
        let range = range.into_range();
        self.inner.lock().await.model.get_text_range(range).map(|s| s.to_string())
    }

    async fn set_text(
        self,
        transaction_id: &ftext::TransactionId,
        old_range: &ftext::Range,
        new_text: String,
    ) -> Result<(), TextFieldError> {
        let old_range = old_range.into_range();
        self.inner.lock().await.model.set_text_range(transaction_id, old_range, new_text)
    }

    async fn set_selection(
        self,
        transaction_id: &ftext::TransactionId,
        selection: ftext::Selection,
    ) -> Result<(), TextFieldError> {
        self.inner.lock().await.model.set_selection(transaction_id, selection)
    }

    async fn begin_transaction(
        self,
        revision_id: &ftext::RevisionId,
    ) -> Result<ftext::TransactionId, TextFieldError> {
        self.inner.lock().await.model.begin_transaction(revision_id)
    }

    async fn commit_transaction(
        self,
        transaction_id: &ftext::TransactionId,
    ) -> Result<ftext::TextFieldState, TextFieldError> {
        let model = &mut self.inner.lock().await.model;
        model.commit_transaction(transaction_id)?;
        Ok(model.state())
    }

    async fn commit_transaction_in_composition(
        self,
        transaction_id: ftext::TransactionId,
        composition_update: Option<FidlCompositionUpdate>,
    ) -> Result<ftext::TextFieldState, TextFieldError> {
        let composition_update =
            composition_update.ok_or_else(|| TextFieldError::InvalidArgument)?.try_into()?;
        let model = &mut self.inner.lock().await.model;
        model.commit_transaction_in_composition(&transaction_id, composition_update)?;
        Ok(model.state())
    }

    async fn cancel_transaction(
        self,
        transaction_id: &ftext::TransactionId,
    ) -> Result<ftext::TextFieldState, TextFieldError> {
        let model = &mut self.inner.lock().await.model;
        model.cancel_transaction(transaction_id)?;
        Ok(model.state())
    }

    async fn begin_composition(
        self,
        revision_id: &ftext::RevisionId,
    ) -> Result<(), TextFieldError> {
        self.inner.lock().await.model.begin_composition(revision_id)
    }

    async fn complete_composition(&self) -> Result<ftext::TextFieldState, TextFieldError> {
        let model = &mut self.inner.lock().await.model;
        model.complete_composition()?;
        Ok(model.state())
    }

    async fn cancel_composition(&self) -> Result<ftext::TextFieldState, TextFieldError> {
        let model = &mut self.inner.lock().await.model;
        model.cancel_composition()?;
        Ok(model.state())
    }
}

/// Converts any `Debug` error in a `Result` into `anyhow::Error`.
trait ToAnyhowError<T, F>
where
    F: std::fmt::Debug,
{
    fn to_anyhow_error(self) -> anyhow::Result<T>;
}

impl<T, F> ToAnyhowError<T, F> for Result<T, F>
where
    F: std::fmt::Debug,
{
    fn to_anyhow_error(self) -> anyhow::Result<T> {
        self.map_err(|e| format_err!("{:?}", e))
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        anyhow::format_err,
        fidl::endpoints::{create_proxy_and_stream, ClientEnd, ServerEnd},
        fidl_fuchsia_input_text_ext::IntoFuchsiaTextRange,
        futures::prelude::*,
        std::{collections::HashMap, ops::Deref},
    };

    #[ignore = "currently broken. fxbug.dev/83711"]
    #[fuchsia::test]
    async fn smoke_test() {
        let field_options = ftext::TextFieldOptions {
            input_type: Some(ftext::InputType::Text),
            ..ftext::TextFieldOptions::EMPTY
        };
        let controller = TextEditController::new("text-edit", field_options)
            .await
            .expect("TextEditController::new");

        let (server_proxy, server_request_stream) =
            create_proxy_and_stream::<ftext::TextEditServer_Marker>()
                .expect("create_proxy_and_stream");

        let server = Server::new();
        let server_future = server.clone().run(server_request_stream);
        fasync::Task::spawn(server_future).detach();
        let controller_future = controller
            .clone()
            .connect_to_text_edit_server(server_proxy.clone())
            .await
            .expect("connect_to_text_edit_server");

        let text_field_proxy =
            server.get_field_snapshots().await.keys().next().expect("one text field proxy").clone();
        server
            .send_edit_command(text_field_proxy.clone(), EditCommand::Type("abc".to_string()))
            .await
            .expect("send_edit_command");

        let snapshot = server
            .get_field_snapshots()
            .await
            .get(&text_field_proxy)
            .expect("get snapshot")
            .clone();
        assert_eq!("abc", snapshot.text.as_ref().unwrap());
        assert_eq!(
            &ftext::Selection { base: 3, extent: 3, affinity: ftext::TextAffinity::Downstream },
            snapshot.state.as_ref().unwrap().selection.as_ref().unwrap()
        );

        server
            .send_edit_command(text_field_proxy.clone(), EditCommand::MoveCaret(-1))
            .await
            .expect("send_edit_command");
        server
            .send_edit_command(text_field_proxy.clone(), EditCommand::Backspace)
            .await
            .expect("send_edit_command");

        let snapshot = server
            .get_field_snapshots()
            .await
            .get(&text_field_proxy)
            .expect("get snapshot")
            .clone();
        assert_eq!("ac", snapshot.text.as_ref().unwrap());
        assert_eq!(
            &ftext::Selection { base: 1, extent: 1, affinity: ftext::TextAffinity::Downstream },
            snapshot.state.as_ref().unwrap().selection.as_ref().unwrap()
        );
    }

    /// Needed because `TextField_Proxy` doesn't implement `Eq` and `Hash`.
    #[derive(Debug, Clone)]
    struct ProxyWrapper<P: fidl::endpoints::Proxy>(P);

    impl<P: fidl::endpoints::Proxy> AsRef<P> for ProxyWrapper<P> {
        fn as_ref(&self) -> &P {
            &self.0
        }
    }

    impl<P: fidl::endpoints::Proxy> Deref for ProxyWrapper<P> {
        type Target = P;

        fn deref(&self) -> &Self::Target {
            &self.0
        }
    }

    impl<P: fidl::endpoints::Proxy> PartialEq for ProxyWrapper<P> {
        fn eq(&self, other: &Self) -> bool {
            self.0.as_channel().as_ref() == other.0.as_channel().as_ref()
        }
    }

    impl<P: fidl::endpoints::Proxy> Eq for ProxyWrapper<P> {}

    impl<P: fidl::endpoints::Proxy> std::hash::Hash for ProxyWrapper<P> {
        fn hash<H: std::hash::Hasher>(&self, state: &mut H) {
            self.0.as_channel().as_ref().hash(state);
        }
    }

    impl<P: fidl::endpoints::Proxy> From<P> for ProxyWrapper<P> {
        fn from(proxy: P) -> Self {
            ProxyWrapper(proxy)
        }
    }

    /// Rudimentary edit commands supported by this test server.
    #[derive(Debug, Clone)]
    enum EditCommand {
        Type(String),
        MoveCaret(i16),
        Backspace,
    }

    /// Rudimentary text server used for testing.
    #[derive(Debug, Clone)]
    struct Server {
        fields: Arc<Mutex<HashMap<ProxyWrapper<ftext::TextFieldProxy>, FieldSnapshot>>>,
    }

    #[derive(Debug, Clone)]
    struct FieldSnapshot {
        options: ftext::TextFieldOptions,
        text: Option<String>,
        state: Option<ftext::TextFieldState>,
    }

    impl Server {
        pub fn new() -> Self {
            Server { fields: Arc::new(Mutex::new(HashMap::new())) }
        }

        async fn run(self, stream: ftext::TextEditServer_RequestStream) {
            use ftext::TextEditServer_Request::*;
            let self_ = self.clone();
            let fut = stream
                .try_for_each_concurrent(2, move |request| {
                    tracing::info!("TextEditServer_Request: {}", request.method_name());
                    let self_ = self_.clone();
                    async move {
                        match request {
                            RegisterFocusedTextField {
                                text_field,
                                server_session,
                                options,
                                responder,
                            } => {
                                let mut result =
                                    self_.register_field(text_field, server_session, options).await;
                                responder.send(&mut result)?;
                                Ok(())
                            }
                        }
                    }
                })
                .await;
            tracing::info!("Finished Server::run");
        }

        async fn register_field(
            &self,
            text_field: ClientEnd<ftext::TextFieldMarker>,
            server_session: ServerEnd<ftext::TextEditServerSessionMarker>,
            options: ftext::TextFieldOptions,
        ) -> Result<(), ftext::TextEditServerError> {
            // TODO: Add new error types to enum
            let proxy = text_field.into_proxy().expect("into_proxy").into();
            let mut fields = self.fields.lock().await;
            if fields.contains_key(&proxy) {
                return Err(ftext::TextEditServerError::AlreadyRegistered);
            }

            let self_ = self.clone();
            let proxy_ = proxy.clone();
            let session_future = server_session.into_stream().expect("into_stream").try_for_each(
                move |req: ftext::TextEditServerSessionRequest| {
                    tracing::info!("{:#?}", &req);
                    let self_ = self_.clone();
                    let proxy_ = proxy_.clone();
                    async move {
                        match req {
                            ftext::TextEditServerSessionRequest::NotifyStateChanged {
                                state,
                                responder,
                            } => {
                                self_
                                    .update_field_snapshot(proxy_, state)
                                    .await
                                    .expect("update_field_snapshot");
                                Ok(())
                            }
                            _ => todo!(),
                        }
                    }
                },
            );
            let session_task = fasync::Task::local(session_future);

            let snapshot = FieldSnapshot { options, text: None, state: None };
            fields.insert(proxy, snapshot);

            Ok(())
        }

        async fn update_field_snapshot(
            &self,
            text_field: ProxyWrapper<ftext::TextFieldProxy>,
            state: ftext::TextFieldState,
        ) -> Result<(), ftext::TextEditServerError> {
            let text = text_field
                .get_text(&mut state.contents_range.expect("contents_range").clone())
                .await
                .expect("connecting to text field")
                .expect("getting text");
            let mut fields = self.fields.lock().await;
            let field_state = fields.get_mut(&text_field).expect("field is registered");
            field_state.text = Some(text);
            field_state.state = Some(state);
            Ok(())
        }

        pub async fn send_edit_command(
            &self,
            text_field: ProxyWrapper<ftext::TextFieldProxy>,
            command: EditCommand,
        ) -> Result<(), Error> {
            let fields = self.fields.lock().await;
            let state =
                fields.get(&text_field).expect("registered field").state.as_ref().expect("state");
            let mut revision_id =
                state.revision_id.as_ref().map(|x| x.clone()).expect("revision_id");
            let mut transaction_id =
                text_field.begin_transaction(&mut revision_id).await?.to_anyhow_error()?;
            let old_selection = state.selection.ok_or_else(|| format_err!("Missing selection"))?;
            let old_range = old_selection.into_range();
            match command {
                EditCommand::Type(s) => {
                    text_field
                        .set_text(
                            &mut transaction_id.clone(),
                            &mut old_range.into_fuchsia_text_range(),
                            s.as_str(),
                        )
                        .await?
                }
                EditCommand::MoveCaret(i) => {
                    let new_position = if i < 0 {
                        old_selection.extent.checked_add(i as u32)
                    } else {
                        old_selection.extent.checked_sub((i * -1) as u32)
                    }
                    .ok_or_else(|| format_err!("Invalid caret position"))?;
                    let mut new_selection = ftext::Selection {
                        base: new_position,
                        extent: new_position,
                        affinity: ftext::TextAffinity::Downstream,
                    };
                    text_field
                        .set_selection(&mut transaction_id.clone(), &mut new_selection)
                        .await?
                }
                EditCommand::Backspace => {
                    let mut range_to_delete = if old_range.is_empty() {
                        ftext::Range { start: old_selection.extent - 1, end: old_selection.extent }
                    } else {
                        old_range.into_fuchsia_text_range()
                    };
                    text_field
                        .set_text(&mut transaction_id.clone(), &mut range_to_delete, "")
                        .await?
                }
            }
            .to_anyhow_error()?;

            let new_state =
                text_field.commit_transaction(&mut transaction_id).await?.to_anyhow_error()?;
            self.clone().update_field_snapshot(text_field, new_state).await.to_anyhow_error()?;
            Ok(())
        }

        pub async fn get_field_snapshots(
            &self,
        ) -> HashMap<ProxyWrapper<ftext::TextFieldProxy>, FieldSnapshot> {
            self.fields.lock().await.clone()
        }
    }
}
