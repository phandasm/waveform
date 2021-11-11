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

DECORATE_SSE41
static inline float sum_product_sse41(const float *a, const float *b) // b must be 16-byte aligned
{
    return _mm_cvtss_f32(_mm_dp_ps(_mm_loadu_ps(a), _mm_load_ps(b), 0xf1));
}

DECORATE_SSE41
static inline double sum_product_sse41(const double *a, const double *b) // b must be 16-byte aligned
{
    return _mm_cvtsd_f64(_mm_dp_pd(_mm_loadu_pd(a), _mm_load_pd(b), 0xf1));
}

#if 0
DECORATE_AVX
static inline float sum_product_avx(const float *a, const float *b) // b must be 32-byte aligned
{
    auto tmp = _mm256_dp_ps(_mm256_loadu_ps(a), _mm256_load_ps(b), 0xf1);
    auto sum = _mm_add_ss(_mm256_extractf128_ps(tmp, 1), _mm256_castps256_ps128(tmp));
    return _mm_cvtss_f32(sum);
}
#endif

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
    sigma = std::abs(sigma) + 1;
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
DECORATE_SSE41
T weighted_avg_sse41(const std::vector<T>& samples, const Kernel<T>& kernel, intmax_t index)
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
        auto i = start;
        for(; i < ssestop; i += step)
            sum += sum_product_sse41(&samples[i], &kernel.weights[i - start]);
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
DECORATE_SSE41
std::vector<T> apply_filter_sse41(const std::vector<T>& samples, const Kernel<T>& kernel)
{
    auto sz = samples.size();
    std::vector<T> filtered;
    filtered.resize(sz);
    for(auto i = 0u; i < sz; ++i)
        filtered[i] = weighted_avg_sse41(samples, kernel, i);
    return filtered;
}
