#include <catch2/catch_test_macros.hpp>

import ffmpeg.rational;
import ffmpeg.rational.io;

namespace
{
constexpr auto kRationalTestTag = "[rational]";
using ffmpeg::Rational;
} // namespace

TEST_CASE("Rational addition", kRationalTestTag)
{
	const auto r = Rational{ 1, 3 } + Rational{ 1, 6 };

	REQUIRE(r.Num() == 1);
	REQUIRE(r.Den() == 2);
}

TEST_CASE("Rational subtraction", kRationalTestTag)
{
	const auto r = Rational{ 1, 2 } - Rational{ 1, 6 };

	REQUIRE(r.Num() == 1);
	REQUIRE(r.Den() == 3);
}

TEST_CASE("Rational multiplication", kRationalTestTag)
{
	const auto r = Rational{ 2, 3 } * Rational{ 3, 4 };
	REQUIRE(r.Num() == 1);
	REQUIRE(r.Den() == 2);
}

TEST_CASE("Rational division", kRationalTestTag)
{
	const auto r = Rational{ 3, 4 } / Rational{ 2, 5 };
	REQUIRE(r.Num() == 15);
	REQUIRE(r.Den() == 8);
}

TEST_CASE("Rational equality", kRationalTestTag)
{
	constexpr Rational r1{ 2, 4 };
	constexpr Rational r2{ 1, 2 };
	constexpr Rational r3{ 3, 4 };
	REQUIRE(r1 == r2);
	REQUIRE(r1 != r3);
	REQUIRE(!(r1 == r3));
	REQUIRE(!(r1 != r2));
}

TEST_CASE("Rational ordering", kRationalTestTag)
{
	constexpr Rational r1{ 1, 3 };
	constexpr Rational r2{ 1, 2 };
	constexpr Rational r3{ 2, 3 };
	REQUIRE(r1 < r2);
	REQUIRE(r2 < r3);
	REQUIRE(r3 > r1);
	REQUIRE(r2 >= r1);
	REQUIRE(r2 >= r1);
	REQUIRE(r2 <= r3);
	REQUIRE(r2 <= r2);
}