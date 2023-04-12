mod error;
mod config;
mod ffmpeg;
mod camera;
mod storage;

fn main() {
    const SECOND: std::time::Duration = std::time::Duration::from_secs(1);
    let config = config::read();
    ffmpeg::prepare();
    let cameras = camera::Cameras::from(&config);
    let mut cameras_with_handles = camera::record_init(&cameras);
    loop {
        camera::record_ensure_working(&cameras, &mut cameras_with_handles);
        std::thread::sleep(SECOND);
    }
}