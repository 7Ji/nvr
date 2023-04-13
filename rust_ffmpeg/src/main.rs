mod error;
mod config;
mod ffmpeg;
mod camera;
mod storage;

use camera::Camera;

fn main() {
    const SECOND: std::time::Duration = std::time::Duration::from_secs(1);
    let config = config::read();
    ffmpeg::prepare();
    let (cameras, metadata) = Camera::from_config(&config);
    let mut handles = Camera::record_init(&cameras, &metadata);
    loop {
        Camera::record_ensure_working(&cameras, &metadata, &mut handles);
        std::thread::sleep(SECOND);
    }
}