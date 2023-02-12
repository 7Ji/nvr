#include <iostream>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>

class Camera {
  public:
    Camera(
        const char * const name,
        const char * const host,
        const char * const path
    ) {
        uint lenName = std::strlen(name);
        if (lenName > _nameMaxLen) {
            throw std::runtime_error("Name too long");
        } else if (lenName == 0) {
            throw std::runtime_error("Name is empty");
        }
        _lenName = lenName;
        std::strncpy(_name, name, lenName);
        std::snprintf(_url, _urlMaxLen, "rtsp://%s/%s", host, path);
        std::snprintf(_pathFormat, _urlMaxLen, "hot/%s_%%Y%%m%%d_%%H%%M%%S.mkv", name);
    }
    void printUrl() {
        std::puts(_url);
    }
    void start() {
        __pid_t pid = fork();
        if (pid < 0) {
            throw std::runtime_error("Failed to fork");
        }
        if (pid > 0) { // parent
            _pid = pid;
            return;
        }
        time_t timeNow, timeFuture, timeDiff;
        tm tmStructNow, tmStructFuture;
        int minute;
        char path[_urlMaxLen];
        while (true) {
            timeNow = time(NULL);
            localtime_r(&timeNow, &tmStructNow);
            strftime(path, _urlMaxLen, _pathFormat, &tmStructNow);
            tmStructFuture = tmStructNow;
            minute = (tmStructNow.tm_min + 11) / 10 * 10;
            if (minute >= 60) {
                tmStructFuture.tm_min = minute - 60;
                ++tmStructFuture.tm_hour;
            } else {
                tmStructFuture.tm_min = minute;
            }
            tmStructFuture.tm_sec = 0;
            timeFuture = mktime(&tmStructFuture);
            timeDiff = timeFuture - timeNow;
            if (timeDiff <= 59) {
                throw std::runtime_error("Duration time too short");
            }
            record(path, timeDiff);
        }
    }

    void record(const char * const path, time_t const duration) {
        __pid_t pid = fork();
        if (pid < 0) {
            throw std::runtime_error("Failed to fork");
        }
        if (pid > 0) {
            sleep(duration);
        } else {
            int fdNull = open("/dev/null", O_WRONLY | O_CREAT, 0666);
            dup2(fdNull, 1);
            dup2(fdNull, 2);
            char durationStr[64];
            snprintf(durationStr, 64, "%ld", duration + 10);
            execl(
              /* Executable */
                "/usr/bin/ffmpeg",
              /* Itself */
                "/usr/bin/ffmpeg",
              /* Input */
                "-use_wallclock_as_timestamps", "1",
                "-i", _url,
              /* Codec */
                "-c", "copy",
              /* Duration */
                "-t", durationStr,
              /* Output */
                "-y", path,
              /* Sentry */
                NULL
            );
        }
    }

    void wait() {
        int status;
        waitpid(_pid, &status, 0);
    }

  protected:
    static const uint _nameMaxLen = 128;
    static const uint _urlMaxLen = 1024;
  private:
    char _name[_nameMaxLen];
    uint _lenName;
    char _url[_urlMaxLen];
    char _pathFormat[_urlMaxLen];
    __pid_t _pid;
};

class Directory {
  public:
    Directory(const char *const path) {
        std::strncpy(_path, path, _pathMaxLen);
    }
  protected:
    static const uint _pathMaxLen = 128;
  private:
    char _path[_pathMaxLen];
};

class HotDirectory: public Directory {
  public:
    HotDirectory(const char *const path) : Directory(path) {
        
    }
};

class ArchivedDirectory: public Directory {
    ArchivedDirectory(const char *const path) : Directory(path) {
        
    }
};


int main() {
    const char host[] = "127.0.0.1:8554";
    Camera cameras[] = {
        Camera("Rooftop", host, "rooftop"),
        Camera("Road", host, "road"),
        Camera("Garden", host, "garden"),
    };
    for (Camera &camera : cameras) {
        camera.start();
    }
    for (Camera &camera : cameras) {
        camera.wait();
    }
    return 0;
}