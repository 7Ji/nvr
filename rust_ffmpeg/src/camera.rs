use std::iter::zip;
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

pub(crate) struct Metadata {
    pub offset: time::UtcOffset,
    pub time_formatter: OwnedFormatItem,
    pub folder: String,
    pub suffix: String,
    pub segment: u32,
    pub stop_delay: time::Duration,
}

// type HandleMap<'a> = HashMap<&'a Arc<Camera>, std::thread::JoinHandle<Result<(), Error>>>;

pub(crate) type CameraHandles = Vec<std::thread::JoinHandle<Result<(), Error>>>;

impl Camera {
    pub(crate) fn from_config(config : &config::Config) -> (Vec<Arc<Self>>, Arc<Metadata>) {
        (
            config
                .cameras
                .iter()
                .map(|camera| Arc::new(Camera {
                    name: camera.name.clone(),
                    url: camera.url.clone(),
                })).collect(),

            Arc::new({
                let segment = config.time.segment;
                let stop_delay = config.time.stop_delay;
                println!("Segment length {}(seconds), stop delay {}(seconds)", segment, stop_delay);
                if segment > 3600 {
                    panic!("Segment too long, it could only be as long as 3600");
                }
                if segment <= 5 {
                    panic!("Segment too long, it could not be shorter or equal to 5 seconds");
                }
                if stop_delay >= segment {
                    panic!("Stop delay too long, it could only be less than segment");
                }
                if 3600 % segment > 0 {
                    panic!("Segment could not devide 3600")
                };
                Metadata { 
                    offset: time::UtcOffset::current_local_offset().expect("Failed to get UTC offset"),
                    time_formatter: time::format_description::parse_owned::<2>(&config.time.naming)
                        .expect("Failed to parse formatter"),
                    folder: config.storages.first().expect("Failed to get first storage").name.clone(),
                    suffix: config.suffix.clone(),
                    segment,
                    stop_delay: time::Duration::seconds(stop_delay as _)
                }})
        )
    }

    pub(crate) fn record_init(cameras: &Vec<Arc<Self>>, metadata: &Arc<Metadata>) -> CameraHandles {
        cameras.iter().map(|camera| {
            let camera_cloned = Arc::clone(camera);
            let metadata_cloned = Arc::clone(&metadata);
            std::thread::spawn(move || 
                ffmpeg::mux_segmented(
                    &camera_cloned, &metadata_cloned
                )
            )
        }).collect()
    }

    pub(crate) fn record_ensure_working(cameras: &Vec<Arc<Self>>, metadata: &Arc<Metadata>, handles: &mut CameraHandles) {
        for (camera, handle) in zip(cameras.iter(), handles.iter_mut()) {
            if handle.is_finished() {
                let camera_cloned = Arc::clone(&camera);
                let metadata_cloned = Arc::clone(&metadata);
                let new_handle = std::thread::spawn(move ||
                    ffmpeg::mux_segmented(
                        &camera_cloned, &metadata_cloned
                    )
                );
                let old_handle = std::mem::replace(handle, new_handle);
                if let Err(e) = old_handle.join().expect("Failed to join camera thread") {
                    println!("Something wrong on camera {}: {:?}, restarted it", camera.name, e);
                }
            }
        }
    }
}