use std::sync::Arc;

use crate::{
    error::Error,
    config,
    ffmpeg,
};
use time;
use time::format_description::OwnedFormatItem;

pub(crate) struct Camera {
    pub name: String,
    pub url: String,
}

pub(crate) struct CamerasMetadata {
    pub offset: time::UtcOffset,
    pub time_formatter: OwnedFormatItem,
    pub folder: String,
    pub suffix: String,
}

pub(crate) struct Cameras {
    cameras: Vec<Arc<Camera>>,
    metadata: Arc<CamerasMetadata>,
}

impl From<&config::Config> for Cameras {
    fn from(config : &config::Config) -> Self {
        let folder = config
            .storages
            .first()
            .expect("Failed to get first storage")
            .name
            .clone();
        Cameras {
            cameras: config
                .cameras
                .iter()
                .map(|camera| Arc::new(Camera {
                    name: camera.name.clone(),
                    url: camera.url.clone(),
                })).collect(),
            metadata: Arc::new(CamerasMetadata { 
                offset: time::UtcOffset::current_local_offset().expect("Failed to get UTC offset"),
                time_formatter: time::format_description::parse_owned::<2>(&config.naming)
                    .expect("Failed to parse formatter"),
                folder,
                suffix: config.suffix.clone(),
            }),
        }
    }
}

struct CameraWithHandle<'a> {
    camera: &'a Arc<Camera>,
    handle: std::thread::JoinHandle<Result<(), Error>>,
}

pub(crate) fn record_all(cameras: Cameras) {
    let mut cameras_with_threads = vec![];
    for camera in cameras.cameras.iter() {
        let camera_cloned = Arc::clone(camera);
        let metadata_cloned = Arc::clone(&cameras.metadata);
        cameras_with_threads.push(CameraWithHandle{
            camera,
            handle: std::thread::spawn(move || 
                ffmpeg::mux_segmented(
                    &camera_cloned, &metadata_cloned
                )
            )
        })
    }
    const SECOND: std::time::Duration = std::time::Duration::from_secs(1);
    loop {
        let mut id = 0;
        while id < cameras_with_threads.len() {
            if cameras_with_threads.get(id).expect("Failed to get camera with thread").handle.is_finished() {
                let camera_with_thread = cameras_with_threads.swap_remove(id);
                let camera_cloned = Arc::clone(&camera_with_thread.camera);
                let metadata_cloned = Arc::clone(&cameras.metadata);
                if let Err(e) = camera_with_thread.handle.join()
                    .expect("Failed to join") {
                    println!("Something wrong on camera {}: {:?}, but we ignore that", camera_with_thread.camera.name, e);
                }
                cameras_with_threads.push(CameraWithHandle{
                    camera: camera_with_thread.camera,
                    handle: std::thread::spawn(move || 
                        ffmpeg::mux_segmented(
                            &camera_cloned, &metadata_cloned
                        )
                    )
                });
            } else {
                id += 1;
            }
        }
        std::thread::sleep(SECOND);
    }
}