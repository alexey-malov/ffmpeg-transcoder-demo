module;
#include "../src/rational.hpp"
module ffmpeg.rational;

namespace ffmpeg
{

std::strong_ordering operator<=>(const Rational& a, const Rational& b) noexcept
{
	const int r = av_cmp_q(a.ToAV(), b.ToAV());
	return r < 0 ? std::strong_ordering::less
		: r > 0	 ? std::strong_ordering::greater
				 : std::strong_ordering::equal;
}

} // namespace ffmpeg