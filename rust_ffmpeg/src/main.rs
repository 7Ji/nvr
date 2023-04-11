mod error;
mod config;
mod ffmpeg;
mod camera;
mod storage;

fn main() {
    let config = config::read();
    ffmpeg::prepare();
    let cameras = camera::Cameras::from(&config);
    std::thread::spawn(|| camera::record_all(cameras)).join().unwrap();
}