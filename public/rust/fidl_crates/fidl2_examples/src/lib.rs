#![feature(conservative_impl_trait)]

include!(concat!(env!("FIDL_GEN_ROOT"), "/garnet/go/src/fidl/examples/example-9.fidl.rs"));

#[cfg(test)]
mod test {
    use super::*;
    use futures::{IntoFuture, FutureExt};

    fn echo_server(channel: async::Channel) -> impl Future<Item = (), Error = fidl::Error> {
        EchoImpl {
            state: (),
            echo32: |_, mut x, res| res.send(&mut x).into_future().recover(|_| ()),
            echo64: |_, mut x, res| res.send(&mut x).into_future().recover(|_| ()),
            echo_enum: |_, mut x, res| res.send(&mut x).into_future().recover(|_| ()),
            echo_handle: |_, mut x, res| res.send(&mut x).into_future().recover(|_| ()),
            echo_channel: |_, mut x, res| res.send(&mut x).into_future().recover(|_| ()),
        }.serve(channel)
    }

    #[test]
    fn echo_once() {
        let mut exec = async::Executor::new().unwrap();

        let (server_chan, client_chan) = zx::Channel::create().unwrap();
        let server_chan = async::Channel::from_channel(server_chan).unwrap();
        let client_chan = async::Channel::from_channel(client_chan).unwrap();

        let server =
            echo_server(server_chan)
                .recover(|e| panic!("server error: {:?}", e));
        async::spawn(server);

        let client = EchoProxy::new(client_chan);

        let test_fut =
            client
                .echo64(&mut 5)
                .and_then(|res| {
                    assert_eq!(res, 5);
                    Ok(())
                });

        exec.run_singlethreaded(test_fut).unwrap();
    }
}

