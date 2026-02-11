module;
#include "../src/rational.hpp"

export module ffmpeg.rational;

import std;

namespace ffmpeg
{

export class Rational
{
public:
	constexpr Rational() noexcept
		: m_value{ 0, 1 }
	{
	}

	constexpr Rational(int num, int den = 1) noexcept
		: m_value{ num, den }
	{
	}

	explicit constexpr Rational(AVRational value) noexcept
		: m_value{ value }
	{
	}

	[[nodiscard]] constexpr int Num() const noexcept { return m_value.num; }
	[[nodiscard]] constexpr int Den() const noexcept { return m_value.den; }

	[[nodiscard]] constexpr AVRational ToAV() const noexcept { return m_value; }

	[[nodiscard]] friend Rational operator+(const Rational& a, const Rational& b) noexcept
	{
		return Rational{ av_add_q(a.m_value, b.m_value) };
	}

	[[nodiscard]] friend Rational operator-(const Rational& a, const Rational& b) noexcept
	{
		return Rational{ av_sub_q(a.m_value, b.m_value) };
	}

	[[nodiscard]] friend Rational operator*(const Rational& a, const Rational& b) noexcept
	{
		return Rational{ av_mul_q(a.m_value, b.m_value) };
	}

	[[nodiscard]] friend Rational operator/(const Rational& a, const Rational& b) noexcept
	{
		return Rational{ av_div_q(a.m_value, b.m_value) };
	}

	[[nodiscard]] friend std::strong_ordering operator<=>(const Rational& a, const Rational& b) noexcept;
	[[nodiscard]] friend bool operator==(const Rational& a, const Rational& b) noexcept
	{
		return operator<=>(a, b) == std::strong_ordering::equal;
	}

private:
	AVRational m_value;
};

} // namespace ffmpeg
