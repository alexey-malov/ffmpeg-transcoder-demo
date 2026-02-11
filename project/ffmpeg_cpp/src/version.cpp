module;
#include "avutil.hpp"

module ffmpeg.version;

namespace ffmpeg {
    FfmpegVersions GetVersions() {
        unsigned ver = avutil_version();
        return { AV_VERSION_MAJOR(ver), AV_VERSION_MINOR(ver), AV_VERSION_MICRO(ver) };
    }
}
