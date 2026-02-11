module; // global module fragment (всЄ до export module Ч вне purview)

export module ffmpeg.version;

namespace ffmpeg
{

	export struct FfmpegVersions {
		int avutil_major = 0;
		int avutil_minor = 0;
		int avutil_micro = 0;
	};

	export FfmpegVersions GetVersions();
}

