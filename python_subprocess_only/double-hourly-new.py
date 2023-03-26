import dataclasses
import asyncio

@dataclasses.dataclass
class Camera:
    url: str
    name: str

    def __post_init__(self):
        self.strftime = f"hot/{self.name}_%y%m%d_%H%M%S.mkv"

    async def get_recorder(self):
        return await asyncio.subprocess.create_subprocess_exec(
            "ffmpeg",
            # Input
                "-use_wallclock_as_timestamps", "1",
                "-rtsp_transport", "tcp",
                "-i", self.url,
            # Codec
                "-c", "copy",
            # Output
                "-f", "segment",
                "-segment_time", "3600",
                "-segment_atclocktime", "1",
                "-reset_timestamps", "1",
                "-strftime", "1",
                self.strftime,
            stdin=asyncio.subprocess.DEVNULL,
            stdout=asyncio.subprocess.DEVNULL,
            stderr=asyncio.subprocess.DEVNULL
        )

    async def record(self):
        print(f"Record for {self.name} started")
        # asyncio.ensure_future()
        for i in itertools.count(1):
            print(f"Recorder {i} for {self.name} started")
            recorder = await self.get_recorder()
            r = await recorder.wait()
            print(f"Recorder {i} for {self.name} returns with {r}")


# ffmpeg -use_wallclock_as_timestamps 1 -rtsp_transport tcp -i rtsp://localhost:8554/rooftop -use_wallclock_as_timestamps 1 -rtsp_transport tcp -i rtsp://localhost:8554/road -use_wallclock_as_timestamps 1 -rtsp_transport tcp -i rtsp://localhost:8554/garden -c copy -map 0:v -map 1:v -map 2:v -map 0:a -map 1:a -map 2:a -f segment -segment_time 3600 -segment_atclocktime 1 -reset_timestamps 1 -strftime 1 '%y%m%d_%H%M%S.mkv'

def _sort_files(file: pathlib.Path):
    return file.stat().st_mtime

async def cleaner():
    hot = "hot"
    archived = "archived"
    path_hot = pathlib.Path(hot)
    path_archived = pathlib.Path(archived)
    while True:
        usage_archived = shutil.disk_usage(archived)
        percent_archived = usage_archived.used / usage_archived.total
        if percent_archived > 0.95:
            print("Archived used too much (>95%), need to clean")
            videos_archived = sorted(path_archived.glob("*.mkv"), key=_sort_files)
            while percent_archived > 0.9:
                try:
                    videos_archived.pop(0).unlink(True)
                except IndexError:
                    print("Archived is empty, re-sort the list")
                    videos_archived = sorted(path_archived.glob("*.mkv"), key=_sort_files)
                    try:
                        videos_archived.pop(0).unlink(True)
                    except IndexError as error:
                        print("Archived is empty, this should not happen")
                        raise error
                usage_archived = shutil.disk_usage(archived)
                percent_archived = usage_archived.used / usage_archived.total
        usage_hot = shutil.disk_usage(hot)
        percent_hot = usage_hot.used / usage_hot.total
        if percent_hot > 0.9:
            print("Hot used to much (>90%), need to clean")
            videos_hot = sorted(path_hot.glob("*.mkv"), key=_sort_files)
            while percent_hot > 0.2:
                try:
                    oldest = videos_hot.pop(0)
                except IndexError:
                    print("Hot is emptry, re-sort the list")
                    videos_hot = sorted(path_hot.glob("*.mkv"), key=_sort_files)
                    try:
                        oldest = videos_hot.pop(0)
                    except IndexError as error:
                        print("Hot is emptry, this should not happen")
                        raise error
                shutil.move(oldest, f'{archived}/{oldest.name}')
                usage_hot = shutil.disk_usage(hot)
                percent_hot = usage_hot.used / usage_hot.total
        await asyncio.sleep(1)

async def main():
    host = "127.0.0.1"
    cameras = (
        Camera(f"rtsp://{host}:8554/rooftop", "Rooftop"),
        Camera(f"rtsp://{host}:8554/road", "Road"),
        Camera(f"rtsp://{host}:8554/garden", "Garden")
    )
    await asyncio.gather(
        *(camera.record() for camera in cameras),
        cleaner()
    )

if __name__ == '__main__':
    asyncio.run(main())