module;

#include <cassert>

// unique_handle.ixx
export module ffmpeg.unique_handle;

import std;

namespace ffmpeg
{

export template <typename T, typename Deleter>
using UniqueHandle = std::unique_ptr<T, Deleter>;

} // namespace ffmpeg
