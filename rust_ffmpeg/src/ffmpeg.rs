use crate::error::Error;
use time;
use ffmpeg_next as ffmpeg;
use ffmpeg::{format, log, media, Rational, encoder, codec, util, Packet};

const AVERROR_BAD_REQUEST: i32 = 22;

pub fn prepare(){
    ffmpeg::init().expect("Failed to init ffmpeg");
    log::set_level(log::Level::Warning);
}

enum InputStream {
    Valid{
        mapping: u32,
        time_base: Rational,
        parameter: codec::Parameters,
    },
    Invalid,
}

struct Input<'a> {
    streams: Vec<InputStream>,
    count_valid_streams: u32,
    metadata: util::dictionary::Owned<'a>,
    // context: format::context::Input,
}

impl Input<'_> {
    fn new(path: &String) -> Result<(Self, format::context::Input), Error> {
        let context = match format::input(&path) {
            Ok(r) => r,
            Err(_) => return Err(Error::FailedToConnect)
        };
        let mut input = Self {
            streams: vec![],
            count_valid_streams: 0,
            metadata: context.metadata().to_owned(),
            // context,
        };
        for stream in context.streams() {
            input.streams.push(
                match stream.parameters().medium() {
                    media::Type::Audio | media::Type::Video | media::Type::Subtitle => InputStream::Valid {
                        mapping: {
                            let mapping = input.count_valid_streams;
                            input.count_valid_streams += 1;
                            mapping
                        },
                        time_base: stream.time_base(),
                        parameter: stream.parameters(),
                    },
                    _ => InputStream::Invalid,
                }
            )
        }
        Ok((input, context))
    }
}

struct Output {
    name: String,
    context: format::context::Output,
    count_streams: u32,
    pts_offset: i64,
}

impl Output {
    fn new(name: String, input: &Input, pts_offset: i64) -> Output {
        match crate::storage::ensure_parent_folder(&name) {
            Ok(_) => (),
            Err(e) => {
                println!("Failed to ensure parent folder for {} exists with error {}", name, e);
                panic!();
            }
        }
        let none_codec = encoder::find(codec::Id::None);
        let mut output = Output {
            context: format::output(&name).expect("Failed to open file"),
            name,
            count_streams: 0,
            pts_offset,
        };
        for in_stream in input.streams.iter() {
            if let InputStream::Valid { mapping: _, time_base: _, parameter } = in_stream {
                let mut out_stream = output.context.add_stream(none_codec).expect("Failed to add stream");
                out_stream.set_parameters(parameter.clone());
                unsafe {
                    (*out_stream.parameters().as_mut_ptr()).codec_tag = 0;
                }
                output.count_streams += 1;
            }
        }
        output.context.set_metadata(input.metadata.clone());
        output.context.write_header().expect("Failed to write header");
        println!("Opened new output {}", output.name);
        output
    }
    fn write_packet(&mut self, packet: &Packet) -> Result<(), Error> {
        if let Err(e) = packet.write_interleaved(&mut self.context) {
            if let ffmpeg::Error::Other {errno} = e {
                if errno != AVERROR_BAD_REQUEST {
                    println!("Error when writing packet: {:?}", e);
                    return Err(Error::BrokenMux)
                }
            } else {
                println!("Error when writing packet: {:?}", e);
                return Err(Error::BrokenMux)
            }
        }
        Ok(())
    }
    fn adjust_packet_and_write(&mut self, packet: &mut Packet, mapping: usize, time_base: &Rational) -> Result<(), Error> {
        let stream = self.context.stream(mapping).expect("Failed to get stream from output");
        packet.rescale_ts(*time_base, stream.time_base());
        packet.set_position(-1);
        packet.set_stream(mapping);
        match packet.pts() {
            Some(pts) => {
                let pts = pts - self.pts_offset;
                packet.set_pts(Some(pts));
                packet.set_dts(Some(pts));
            },
            None => ()
        }
        if let Err(e) = self.write_packet(packet) {
            return Err(e);
        }
        Ok(())
    }
    fn close(mut self) {
        self.context.write_trailer().expect("Failed to close output");
        println!("Closed output {}", self.name);
    }
}


fn get_time(offset: &time::UtcOffset) -> time::OffsetDateTime {
    time::OffsetDateTime::now_utc().to_offset(*offset)
}

fn get_next_time(time_now: &time::OffsetDateTime, segment: u32) -> time::OffsetDateTime {
    let minute_second = time_now.minute() as u32 * 60 + time_now.second() as u32;
    let upper_bound = (minute_second / segment + 1) * segment;

    time_now.clone() + time::Duration::seconds((upper_bound - minute_second) as _)
}

fn get_name(time: &time::OffsetDateTime, camera: &crate::camera::Camera, cameras_meta: &crate::camera::CamerasMetadata) -> String {
    format!("{}/{}_{}{}", cameras_meta.folder, time.format(&cameras_meta.time_formatter).expect("Failed to format time"), camera.name, cameras_meta.suffix)
}

pub(crate) fn mux_segmented(camera: &crate::camera::Camera, cameras_meta: &crate::camera::CamerasMetadata) -> Result<(), Error>{
    let (input, mut input_context) = match Input::new(&camera.url) {
        Ok((input, input_context)) => (input, input_context),
        Err(e) => return Err(e),
    };
    let mut time_now = get_time(&cameras_meta.offset);
    let mut time_next = get_next_time(&time_now, cameras_meta.segment);
    let mut time_stop = time_next + cameras_meta.stop_delay;
    println!("Muxing {} into segments:\n - from url: {}\n - segment length: {}s\n - stop delay: {}\n - now: {}\n - next: {}\n - stop: {}", camera.name, camera.url, cameras_meta.segment, cameras_meta.stop_delay, time_now, time_next, time_stop);
    let mut output_this = Output::new(get_name(&time_now, camera, cameras_meta), &input, 0);
    let mut output_last: Option<Output> = None;
    for (stream, mut packet) in input_context.packets() {
        time_now = get_time(&cameras_meta.offset);
        if time_now >= time_next {
            if let Some(output) = output_last {
                output.close();
            }
            output_last = Some(output_this);
            output_this = Output::new(get_name(&time_now, camera, cameras_meta), &input, packet.pts().expect("Failed to get pts to set offset"));
            time_next = get_next_time(&time_now, cameras_meta.segment);
        }
        if time_now >= time_stop {
            if let Some(output) = output_last {
                output.close();
                output_last = None;
            }
            time_stop = time_next + cameras_meta.stop_delay;
        }
        match input.streams.get(stream.index()).expect("Failed to get input stream info") {
            InputStream::Invalid => continue,
            InputStream::Valid { mapping, time_base, parameter: _ } => {
                if let Some(output_last) = &mut output_last {
                    let mut packet = packet.clone();
                    if let Err(e) = output_last.adjust_packet_and_write(&mut packet, *mapping as _, time_base) {
                        return Err(e);
                    }
                }
                if let Err(e) = output_this.adjust_packet_and_write(&mut packet, *mapping as _, time_base) {
                    return Err(e);
                }
            }
        }
    }
    Ok(())
} 