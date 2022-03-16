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
#include "aligned_mem.hpp"
#include <cmath>
#include <cstdint>
#include <vector>
#include <type_traits>
#include <immintrin.h>
#include <memory>

// helpers for template SIMD code
template<typename T>
struct SSEType
{
    // empty unspecialized version
};

template<>
struct SSEType<float>
{
    using Type = __m128;
};

template<>
struct SSEType<double>
{
    using Type = __m128d;
};

template<typename T>
DECORATE_SSE2
inline std::enable_if_t<std::is_same_v<T, __m128>, T> setzero()
{
    return _mm_setzero_ps();
}

template<typename T>
DECORATE_SSE2
inline std::enable_if_t<std::is_same_v<T, __m128d>, T> setzero()
{
    return _mm_setzero_pd();
}

DECORATE_AVX
static inline float horizontal_sum(__m128 vec)
{
    auto low = vec;
    auto high = _mm_permute_ps(low, _MM_SHUFFLE(3, 2, 3, 2)); // high[0] = low[2], high[1] = low[3]
    low = _mm_add_ps(high, low); // (h[0] + l[0]) (h[1] + l[1])
    high = _mm_movehdup_ps(low); // high[0] = low[1]
    return _mm_cvtss_f32(_mm_add_ss(high, low));
}

DECORATE_AVX
static inline double horizontal_sum(__m128d vec)
{
    auto vec2 = _mm_permute_pd(vec, 1); // vec2[0] = vec[1]
    return _mm_cvtsd_f64(_mm_add_sd(vec, vec2));
}

DECORATE_AVX
static inline __m128 sum_product_fma3(const float *a, const float *b, __m128 sum) // b must be 16-byte aligned
{
    return _mm_fmadd_ps(_mm_loadu_ps(a), _mm_load_ps(b), sum);
}

DECORATE_AVX
static inline __m128d sum_product_fma3(const double *a, const double *b, __m128d sum) // b must be 16-byte aligned
{
    return _mm_fmadd_pd(_mm_loadu_pd(a), _mm_load_pd(b), sum);
}

template<typename T>
struct Kernel
{
    std::unique_ptr<T[], AVXDeleter> weights;
    int radius = 0;
    int size = 0;
    int sse_size = 0;
    std::enable_if_t<std::is_floating_point_v<T>, T> sum = (T)0;
};

template<typename T>
Kernel<T> make_gauss_kernel(T sigma)
{
    Kernel<T> ret;
    sigma = std::max(std::abs(sigma), (T)0.01);
    auto w = (int)std::ceil((T)3 * sigma);
    auto size = (2 * w) - 1;
    ret.weights.reset(avx_alloc<T>(size));
    ret.radius = w;
    ret.size = size;
    ret.sse_size = size & -(int)(sizeof(__m128) / sizeof(T));
    constexpr auto pi2 = (T)M_PI * (T)2;
    const auto sigsqr = sigma * sigma;
    const auto expdenom = (T)2 * sigsqr;
    const auto coeff = ((T)1 / (pi2 * sigsqr));
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

template<typename T>
T weighted_avg(const std::vector<T>& samples, const Kernel<T>& kernel, intmax_t index)
{
    const auto start = (index - kernel.radius) + 1;
    const auto stop = index + kernel.radius;
    T sum = (T)0;
    if((start < 0) || (stop > (intmax_t)samples.size()))
    {
        T wsum = (T)0;
        for(auto i = std::max(start, (intmax_t)0); i < std::min(stop, (intmax_t)samples.size()); ++i)
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
DECORATE_AVX
T weighted_avg_fma3(const std::vector<T>& samples, const Kernel<T>& kernel, intmax_t index)
{
    const auto start = (index - kernel.radius) + 1;
    const auto stop = index + kernel.radius;
    T sum = (T)0;
    if((start < 0) || (stop > (intmax_t)samples.size()))
    {
        T wsum = (T)0;
        for(auto i = std::max(start, (intmax_t)0); i < std::min(stop, (intmax_t)samples.size()); ++i)
        {
            auto weight = kernel.weights[i - start];
            wsum += weight;
            sum += samples[i] * weight;
        }
        return sum / wsum;
    }
    else
    {
        constexpr auto step = sizeof(__m128) / sizeof(T);
        const auto ssestop = start + kernel.sse_size;
        auto vecsum = setzero<SSEType<T>::Type>();
        auto i = start;
        for(; i < ssestop; i += step)
            vecsum = sum_product_fma3(&samples[i], &kernel.weights[i - start], vecsum);
        sum = horizontal_sum(vecsum);
        for(; i < stop; ++i)
            sum += samples[i] * kernel.weights[i - start];
        return sum / kernel.sum;
    }
}

template<typename T>
std::vector<T> apply_filter(const std::vector<T>& samples, const Kernel<T>& kernel)
{
    auto sz = samples.size();
    std::vector<T> filtered;
    filtered.resize(sz);
    for(auto i = 0u; i < sz; ++i)
        filtered[i] = weighted_avg(samples, kernel, i);
    return filtered;
}

template<typename T>
DECORATE_AVX
std::vector<T> apply_filter_fma3(const std::vector<T>& samples, const Kernel<T>& kernel)
{
    auto sz = samples.size();
    std::vector<T> filtered;
    filtered.resize(sz);
    if(kernel.sse_size >= ((sizeof(typename SSEType<T>::Type) / sizeof(T)) * 2)) // make sure we get at least 2 SIMD iterations
    {
        for(auto i = 0u; i < sz; ++i)
            filtered[i] = weighted_avg_fma3(samples, kernel, i);
    }
    else // otherwise use the plain C version
    {
        for(auto i = 0u; i < sz; ++i)
            filtered[i] = weighted_avg(samples, kernel, i);
    }
    return filtered;
}
