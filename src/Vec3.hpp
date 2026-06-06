#pragma once

#include <cmath>
#include <ostream>

struct Vec3 {
    double x{0.0};
    double y{0.0};
    double z{0.0};

    constexpr Vec3() = default;
    constexpr Vec3(double xValue, double yValue, double zValue)
        : x(xValue), y(yValue), z(zValue) {}

    [[nodiscard]] double lengthSquared() const {
        return x * x + y * y + z * z;
    }

    [[nodiscard]] double length() const {
        return std::sqrt(lengthSquared());
    }

    [[nodiscard]] Vec3 normalized(double eps = 1e-12) const {
        const double len = length();
        if (len < eps) {
            return {};
        }
        return {x / len, y / len, z / len};
    }

    [[nodiscard]] static double dot(const Vec3& a, const Vec3& b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    [[nodiscard]] static Vec3 cross(const Vec3& a, const Vec3& b) {
        return {
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x,
        };
    }

    Vec3& operator+=(const Vec3& other) {
        x += other.x;
        y += other.y;
        z += other.z;
        return *this;
    }

    Vec3& operator-=(const Vec3& other) {
        x -= other.x;
        y -= other.y;
        z -= other.z;
        return *this;
    }

    Vec3& operator*=(double scalar) {
        x *= scalar;
        y *= scalar;
        z *= scalar;
        return *this;
    }

    Vec3& operator/=(double scalar) {
        x /= scalar;
        y /= scalar;
        z /= scalar;
        return *this;
    }
};

[[nodiscard]] inline Vec3 operator+(Vec3 lhs, const Vec3& rhs) {
    lhs += rhs;
    return lhs;
}

[[nodiscard]] inline Vec3 operator-(Vec3 lhs, const Vec3& rhs) {
    lhs -= rhs;
    return lhs;
}

[[nodiscard]] inline Vec3 operator-(const Vec3& value) {
    return {-value.x, -value.y, -value.z};
}

[[nodiscard]] inline Vec3 operator*(Vec3 value, double scalar) {
    value *= scalar;
    return value;
}

[[nodiscard]] inline Vec3 operator*(double scalar, Vec3 value) {
    value *= scalar;
    return value;
}

[[nodiscard]] inline Vec3 operator/(Vec3 value, double scalar) {
    value /= scalar;
    return value;
}

inline std::ostream& operator<<(std::ostream& os, const Vec3& value) {
    os << value.x << ',' << value.y << ',' << value.z;
    return os;
}
