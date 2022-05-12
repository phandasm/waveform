/*
    Copyright (C) 2021 Devin Davila

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#pragma once
#include <cmath>
#include <algorithm>
#include <type_traits>
#include <cstdint>

template<typename T>
std::enable_if_t<std::is_floating_point_v<T>, T> log_interp(T a, T b, T t)
{
    return a * std::pow(b / a, t);
}

template<typename T>
std::enable_if_t<std::is_floating_point_v<T>, T> lerp(T a, T b, T t)
{
    return a + t * (b - a);
}

template<typename T>
std::enable_if_t<std::is_floating_point_v<T>, T> sinc(T x)
{
    if(x == 0.0)
        return 1.0;
    const auto tmp = (T)M_PI * x;
    return std::sin(tmp) / tmp;
}

template<typename T>
std::enable_if_t<std::is_floating_point_v<T>, T> lanczos(T x, T w)
{
    if(std::abs(x) < w)
        return sinc(x) * sinc(x / w);
    return 0.0;
}

template<typename T, typename U>
std::enable_if_t<std::is_floating_point_v<T>, T> lanczos_interp(T x, T w, const size_t len, const U *buf)
{
    T val = 0;
    const auto floorx = (intmax_t)x;
    const auto floorw = (intmax_t)w;
    for(auto i = std::max(floorx - floorw + 1, (intmax_t)0); i <= std::min(floorx + floorw, (intmax_t)len - 1); ++i)
        val += (T)buf[i] * (T)lanczos(x - i, w);
    return val;
}
