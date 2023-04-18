# Network Video Recorder Implementations in various langauges

## Build all C/C++ implementations
```
make
```

## c_pthread_only_with_libffmpeg
 - Written in C
 - Uses pthreads for multi-threading
   - Specially, even if there's only one source, it will still be multi-threading, to cover the potential loss between two sequences (default being 3 seconds overlapping)
 - Directly linked with libffmpeg and use libffmpeg functions so very lightweight
### Run
```
./nvr --storage [storage definition] (--storage [storage definition] (--storage [storage definition] (...)))
      --camera [camera definition] (--camera [camera definition] (--camera [camera definition] (...)))
      (--help)
      (--version)

  - [storage deinition]: [path]:[thresholds]
    - [path]: folder name or path, relative or absolute both fine
    - [thresholds]: [from]:[to]
      - [from]: when free space <= this percent, triggers cleaning
      - [to]: when free space >= this percent, stops cleaning
  - [camera definition]: [name]:[strftime]:[url]
    - [name]: 
    - [strftime]: strftime definition to be used to generate output name
    - [url]: a valid input url for ffmpeg
```

#### Example
```
./nvr --camera rooftop:%Y%m%d/%H00/%Y%m%d_%H%M%S_rooftop:rtsp://127.0.0.1:8554/rooftop --camera road:%Y%m%d/%H00/%Y%m%d_%H%M%S_road:rtsp://127.0.0.1:8554/road --camera garden:%Y%m%d/%H00/%Y%m%d_%H%M%S_garden:rtsp://127.0.0.1:8554/garden --storage 0-hot:10:90 --storage 1-warmer:3:5 --storage 2-warm:1:3 --storage 3-cold:1:2
```

## c_fork_only_with_ffmpeg
 - Written in C
 - Uses fork for multi-processing
   - Specially, even if there's only one source, it will still be multi-threading, to cover the potential loss between two sequences (default being 10 seconds overlapping)
 - Calls ffmpeg with fork+exec method
   - Overhead could be introduced for invoking ffmpeg
### Run
```
./nvr --storage [storage definition] (--storage [storage definition] (--storage [storage definition] (...)))
      --camera [camera definition] (--camera [camera definition] (--camera [camera definition] (...)))
      --help
      --version

  - [storage deinition]: [path]:[thresholds]
    - [path]: folder name or path, relative or absolute both fine
    - [thresholds]: [from]:[to]
      - [from]: when free space <= this percent, triggers cleaning
      - [to]: when free space >= this percent, stops cleaning
  - [camera definition]: [name]:[url]
    - [name]: 
    - [url]: a valid input url for ffmpeg
```
Example:
```
./nvr --camera rooftop:rtsp://127.0.0.1:8554/rooftop --camera road:rtsp://127.0.0.1:8554/road --camera garden:rtsp://127.0.0.1:8554/garden --storage hot:10:90 --storage warm:5:10 --storage cold:1:5
```

## cpp_fork_only_with_ffmpeg
 - Written in C++
 - Essentially the c_fork_only_with_ffmpeg implementation

## python_subprocess_only
 - Written in Python
 - Essentially the c_fork_only_with_ffmpeg implementation