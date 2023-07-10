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
#include "waveform_config.hpp"
#include "aligned_buffer.hpp"
#include "math_funcs.hpp"
#include <cmath>
#include <cstdint>
#include <vector>
#include <type_traits>
#include <numbers>

template<typename T>
struct Kernel
{
    static_assert(std::is_floating_point_v<T>, "Kernel must be a floating point type.");
    AlignedBuffer<T> weights;
    int radius = 0;
    int size = 0;
    int sse_size = 0;
    int avx_size = 0;
    T sum = (T)0;
};

template<typename T>
Kernel<T> make_gauss_kernel(T sigma)
{
    Kernel<T> ret;
    sigma = std::max(std::abs(sigma), (T)0.01);
    auto w = (int)std::ceil((T)3 * sigma);
    auto size = (2 * w) - 1;
    ret.weights.reset(size);
    ret.radius = w;
    ret.size = size;
    ret.sse_size = size & -(16 / (int)sizeof(T));
    ret.avx_size = size & -(32 / (int)sizeof(T));
    constexpr auto pi2 = std::numbers::pi_v<T> * (T)2;
    const auto sigsqr = sigma * sigma;
    const auto expdenom = (T)2 * sigsqr;
    const auto coeff = ((T)1 / (std::sqrt(pi2) * sigma));
    auto j = 0;
    for(auto i = -w + 1; i < w; ++i)
    {
        auto exponent = -((i * i) / expdenom);
        auto weight = coeff * std::exp(exponent);
        ret.weights[j++] = weight;
        ret.sum += weight;
    }
    return ret;
}

// this creates a rather large lookup table, size * (radius * 2) * sizeof(T) bytes
template<typename T>
Kernel<T> make_lanczos_kernel(const std::vector<T>& indices, const intmax_t radius)
{
    Kernel<T> ret;
    const auto size = (intmax_t)indices.size();
    if((size <= 0) || (radius <= 0))
        return ret;
    const auto ksize = size * (radius * 2);
    ret.weights.reset(ksize);
    ret.radius = (int)radius;
    ret.size = (int)ksize; // size fields currently unused, would probably be more useful if they measured a single node rather than the whole buffer
    ret.sse_size = ksize & -(16 / (int)sizeof(T));
    ret.avx_size = ksize & -(32 / (int)sizeof(T));
    const auto fradius = (T)radius;
    for(intmax_t i = 0; i < size; ++i)
    {
        const auto x = indices[i];
        const auto ix = (intmax_t)x; // NOTE: technically std::floor(x) but negatives are out of our domain so this is slightly faster
        const auto start = ix - radius + 1;
        const auto stop = ix + radius;
        const auto base = i * radius * 2;
        for(auto j = start; j <= stop; ++j)
            ret.weights[base + (j - start)] = lanczos(x - j, fradius);
    }
    return ret;
}

template<typename T>
T weighted_avg(const std::vector<T>& samples, const Kernel<T>& kernel, intmax_t index)
{
    const auto start = (index - kernel.radius) + 1;
    const auto stop = index + kernel.radius;
    T sum = (T)0;
    if((start < 0) || (stop > (intmax_t)samples.size()))
    {
        const auto loopstart = std::max(start, (intmax_t)0);
        const auto loopstop = std::min(stop, (intmax_t)samples.size());
        T wsum = (T)0;
        for(auto i = loopstart; i < loopstop; ++i)
        {
            auto weight = kernel.weights[i - start];
            wsum += weight;
            sum += samples[i] * weight;
        }
        return sum / wsum;
    }
    else
    {
        for(auto i = start; i < stop; ++i)
            sum += samples[i] * kernel.weights[i - start];
        return sum / kernel.sum;
    }
}

template<typename T>
WAV_FORCE_INLINE T lanczos_convolve(const T *samples, size_t sz, const Kernel<T>& kernel, intmax_t index, intmax_t kernel_base)
{
    const auto start = (index - kernel.radius) + 1;
    const auto stop = std::min(index + kernel.radius + 1, (intmax_t)sz);
    T sum = (T)0;
    for(auto i = std::max(start, (intmax_t)0); i < stop; ++i)
        sum += samples[i] * kernel.weights[kernel_base + (i - start)];
    return sum;
}

template<typename T>
std::vector<T>& apply_filter(const std::vector<T>& samples, const Kernel<T>& kernel, std::vector<T>& output)
{
    const auto sz = samples.size();
    if(output.size() < sz)
        output.resize(sz);
    for(auto i = 0u; i < sz; ++i)
        output[i] = weighted_avg(samples, kernel, i);
    return output;
}

template<typename T>
std::vector<T>& apply_lanczos_filter(const T *samples, size_t sz, const std::vector<T>& x, const Kernel<T>& kernel, std::vector<T>& output)
{
    const auto xsz = (intmax_t)x.size();
    const auto d = (intmax_t)kernel.radius * 2;
    if((intmax_t)output.size() < xsz)
        output.resize(xsz);
    for(intmax_t i = 0, j = 0; i < xsz; ++i, j += d)
        output[i] = lanczos_convolve(samples, sz, kernel, (intmax_t)x[i], j);
    return output;
}

// bar graph version
template<typename T>
std::vector<T>& apply_lanczos_filter(const T *samples, size_t sz, const std::vector<int>& band_widths, const std::vector<T>& x, const Kernel<T>& kernel, std::vector<T>& output)
{
    const auto d = (intmax_t)kernel.radius * 2;
    const auto bands = (intmax_t)band_widths.size();
    if((intmax_t)output.size() < bands)
        output.resize(bands);
    for(intmax_t i = 0, k = 0, l = 0; i < bands; ++i)
    {
        auto sum = (T)0;
        auto count = (intmax_t)band_widths[i];
        for(intmax_t j = 0; j < count; ++j, ++k, l += d)
            sum += lanczos_convolve(samples, sz, kernel, (intmax_t)x[k], l);
        output[i] = sum / (T)count;
    }
    return output;
}

#ifndef DISABLE_X86_SIMD

float weighted_avg_fma3(const std::vector<float>& samples, const Kernel<float>& kernel, intmax_t index);
//double weighted_avg_fma3(const std::vector<double>& samples, const Kernel<double>& kernel, intmax_t index);

std::vector<float>& apply_filter_fma3(const std::vector<float>& samples, const Kernel<float>& kernel, std::vector<float>& output);
//std::vector<double>& apply_filter_fma3(const std::vector<double>& samples, const Kernel<double>& kernel, std::vector<double>& output);

// WARNING: these require kernel.radius == 4
std::vector<float>& apply_lanczos_filter_fma3(const float *samples, size_t sz, const std::vector<float>& x, const Kernel<float>& kernel, std::vector<float>& output);

// bar graph version
std::vector<float>& apply_lanczos_filter_fma3(const float *samples, size_t sz, const std::vector<int>& band_widths, const std::vector<float>& x, const Kernel<float>& kernel, std::vector<float>& output);

#endif // !DISABLE_X86_SIMD
