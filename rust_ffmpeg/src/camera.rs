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

impl Clone for Camera {
    fn clone(&self) -> Self {
        Self { name: self.name.clone(), url: self.url.clone() }
    }
}

pub(crate) struct CamerasMetadata {
    pub offset: time::UtcOffset,
    pub time_formatter: OwnedFormatItem,
    pub folder: String,
}

impl Clone for CamerasMetadata {
    fn clone(&self) -> Self {
        Self { offset: self.offset.clone(), time_formatter: self.time_formatter.clone(), folder: self.folder.clone() }
    }
}

pub(crate) struct Cameras {
    cameras: Vec<Camera>,
    metadata: CamerasMetadata,
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
                .map(|camera| Camera {
                    name: camera.name.clone(),
                    url: camera.url.clone(),
                }).collect(),
            metadata: CamerasMetadata { 
                offset: time::UtcOffset::current_local_offset().expect("Failed to get UTC offset"),
                time_formatter: time::format_description::parse_owned::<2>(&config.naming)
                    .expect("Failed to parse formatter"),
                folder
            },
        }
    }
}

struct CameraWithHandle<'a> {
    camera: &'a Camera,
    handle: std::thread::JoinHandle<Result<(), Error>>,
}

pub(crate) fn record_all(cameras: Cameras) {
    let mut cameras_with_threads = vec![];
    for camera in cameras.cameras.iter() {
        let camera_cloned = camera.clone();
        let metadata_cloned = cameras.metadata.clone();
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
            if cameras_with_threads.get(id).unwrap().handle.is_finished() {
                let camera_with_thread = cameras_with_threads.swap_remove(id);
                let camera = camera_with_thread.camera;
                let camera_cloned = camera.clone();
                let metadata_cloned = cameras.metadata.clone();
                if let Err(e) = camera_with_thread.handle.join()
                    .expect("Failed to join") {
                    println!("Something wrong on camera {}: {:?}, but we ignore that", camera.name, e);
                    return;
                }
                cameras_with_threads.push(CameraWithHandle{
                    camera,
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