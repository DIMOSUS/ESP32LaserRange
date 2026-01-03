#pragma once

struct vec2
{
	float x;
	float y;

	vec2() : x(0), y(0) {}
	vec2(float x_, float y_) : x(x_), y(y_) {}

	vec2 operator+(const vec2& v) const { return vec2(x + v.x, y + v.y); }
	vec2 operator-(const vec2& v) const { return vec2(x - v.x, y - v.y); }

	vec2& operator+=(const vec2& v) { x += v.x; y += v.y; return *this; }
	vec2& operator-=(const vec2& v) { x -= v.x; y -= v.y; return *this; }

	vec2 operator*(float s) const { return vec2(x * s, y * s); }
	vec2 operator/(float s) const { return vec2(x / s, y / s); }

	vec2& operator*=(float s) { x *= s; y *= s; return *this; }
	vec2& operator/=(float s) { x /= s; y /= s; return *this; }

	vec2 operator-() const { return vec2(-x, -y); }

	float length() const
	{
		return sqrtf(x * x + y * y);
	}

	float lengthSq() const
	{
		return x * x + y * y;
	}

	vec2 normalized() const
	{
		float len = length();
		if (len == 0.0f) return vec2(0, 0);
		return vec2(x / len, y / len);
	}

	void normalize()
	{
		float len = length();
		if (len != 0.0f)
		{
			x /= len;
			y /= len;
		}
	}

	static float dot(const vec2& a, const vec2& b)
	{
		return a.x * b.x + a.y * b.y;
	}

	static vec2 lerp(const vec2& a, const vec2& b, float t)
	{
		return vec2
		(
			a.x + (b.x - a.x) * t,
			a.y + (b.y - a.y) * t
		);
	}
};