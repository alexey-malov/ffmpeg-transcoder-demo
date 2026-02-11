export module ffmpeg.rational.io;
import ffmpeg.rational;
import std;

namespace ffmpeg
{

export std::ostream& operator<<(std::ostream& os, const Rational& r)
{
	return os << r.Num() << '/' << r.Den();
}

} // namespace ffmpeg
