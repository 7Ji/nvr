#[derive(Debug)]
pub(crate) enum Error {
    FailedToConnect,
    BrokenMux,
    FailedIO,
}