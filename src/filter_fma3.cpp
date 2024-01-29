/*
    Copyright (C) 2022 Devin Davila

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

#include "filter.hpp"
#include "simd_helpers.hpp"
#include <immintrin.h>
#include <cassert>

float weighted_avg_fma3(const std::vector<float>& samples, const Kernel<float>& kernel, intmax_t index)
{
    // NOTE: Initial tests with 'usuable' radius values seemed to reveal performance benefit for 128-bit vectors
    // averaging 2-3 iterations vs 1 iteration of 256-bit, but this could use re-tesing and can definitely be done better in any case.
    const auto start = (index - kernel.radius) + 1;
    const auto stop = index + kernel.radius;
    float sum = 0.0f;
    if((start < 0) || (stop > (intmax_t)samples.size()))
    {
        const auto loopstart = std::max(start, (intmax_t)0);
        const auto loopstop = std::min(stop, (intmax_t)samples.size());
        float wsum = 0.0f;
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
        constexpr auto step = sizeof(__m128) / sizeof(float);
        const auto ssestop = start + kernel.sse_size;
        auto vecsum = _mm_setzero_ps();
        auto i = start;
        for(; i < ssestop; i += step)
            vecsum = _mm_fmadd_ps(_mm_loadu_ps(&samples[i]), _mm_load_ps(&kernel.weights[i - start]), vecsum);
        sum = horizontal_sum(vecsum);
        for(; i < stop; ++i)
            sum += samples[i] * kernel.weights[i - start];
        return sum / kernel.sum;
    }
}

std::vector<float>& apply_filter_fma3(const std::vector<float>& samples, const Kernel<float>& kernel, std::vector<float>& output)
{
    const auto sz = samples.size();
    if(output.size() < sz)
        output.resize(sz);
    if((size_t)kernel.sse_size >= ((sizeof(__m128) / sizeof(float)) * 2)) // make sure we get at least 2 SIMD iterations
    {
        for(auto i = 0u; i < sz; ++i)
            output[i] = weighted_avg_fma3(samples, kernel, i);
    }
    else // otherwise use the plain C version
    {
        for(auto i = 0u; i < sz; ++i)
            output[i] = weighted_avg(samples, kernel, i);
    }
    return output;
}

// specialized for kernel.size = 8
static std::vector<float>& apply_interp_filter_fma3_x8(const float *samples, size_t sz, const std::vector<float>& x, const Kernel<float>& kernel, std::vector<float>& output)
{
    assert(kernel.radius == 4);
    constexpr auto step = sizeof(__m256) / sizeof(float);
    const auto avx_stop = (intmax_t)sz - 4;
    const auto xsz = x.size();
    if(output.size() < xsz)
        output.resize(xsz);
    for(size_t i = 0, j = 0; i < xsz; ++i, j += step)
    {
        auto index = (intmax_t)x[i];
        if((index >= 3) && (index < avx_stop))
            output[i] = horizontal_sum(_mm256_mul_ps(_mm256_loadu_ps(&samples[index - 3]), _mm256_load_ps(&kernel.weights[j])));
        else
        {
            const auto start = index - 3;
            const auto stop = std::min(index + 5, (intmax_t)sz);
            auto sum = _mm_setzero_ps();
            for(auto k = std::max(start, (intmax_t)0); k < stop; ++k)
                sum = _mm_fmadd_ss(_mm_load_ss(&samples[k]), _mm_load_ss(&kernel.weights[j + (k - start)]), sum);
            output[i] = _mm_cvtss_f32(sum);
        }
    }
    return output;
}

// bar graph version
// specialized for kernel.size = 8
static std::vector<float>& apply_interp_filter_fma3_x8(const float *samples, size_t sz, const std::vector<int>& band_widths, const std::vector<float>& x, const Kernel<float>& kernel, std::vector<float>& output)
{
    assert(kernel.radius == 4);
    constexpr auto step = sizeof(__m256) / sizeof(float);
    const auto avx_stop = (intmax_t)sz - 4;
    const auto bands = (intmax_t)band_widths.size();
    if((intmax_t)output.size() < bands)
        output.resize(bands);
    for(intmax_t i = 0, k = 0, l = 0; i < bands; ++i)
    {
        auto vecsum = _mm256_setzero_ps();
        auto count = (intmax_t)band_widths[i];
        for(intmax_t j = 0; j < count; ++j, ++k, l += step)
        {
            auto index = (intmax_t)x[k];
            if((index >= 3) && (index < avx_stop))
                vecsum = _mm256_fmadd_ps(_mm256_loadu_ps(&samples[index - 3]), _mm256_load_ps(&kernel.weights[l]), vecsum);
            else
            {
                const auto start = index - 3;
                const auto stop = std::min(index + 5, (intmax_t)sz);

                // this could be done better with asm, but this'll just have to do
                auto sum = _mm_setzero_ps();
                for(auto m = std::max(start, (intmax_t)0); m < stop; ++m)
                    sum = _mm_fmadd_ss(_mm_load_ss(&samples[m]), _mm_load_ss(&kernel.weights[l + (m - start)]), sum);
                vecsum = _mm256_insertf128_ps(vecsum, _mm_add_ss(_mm256_castps256_ps128(vecsum), sum), 0);
            }
        }
        output[i] = horizontal_sum(vecsum) / count;
    }
    return output;
}

// specialized for kernel.size = 4
static std::vector<float>& apply_interp_filter_fma3_x4(const float *samples, size_t sz, const std::vector<float>& x, const Kernel<float>& kernel, std::vector<float>& output)
{
    assert(kernel.radius == 2);
    constexpr auto step = sizeof(__m128) / sizeof(float);
    const auto sse_stop = (intmax_t)sz - 2;
    const auto xsz = x.size();
    if(output.size() < xsz)
        output.resize(xsz);
    for(size_t i = 0, j = 0; i < xsz; ++i, j += step)
    {
        auto index = (intmax_t)x[i];
        if((index >= 1) && (index < sse_stop))
            output[i] = horizontal_sum(_mm_mul_ps(_mm_loadu_ps(&samples[index - 1]), _mm_load_ps(&kernel.weights[j])));
        else
        {
            const auto start = index - 1;
            const auto stop = std::min(index + 3, (intmax_t)sz);
            auto sum = _mm_setzero_ps();
            for(auto k = std::max(start, (intmax_t)0); k < stop; ++k)
                sum = _mm_fmadd_ss(_mm_load_ss(&samples[k]), _mm_load_ss(&kernel.weights[j + (k - start)]), sum);
            output[i] = _mm_cvtss_f32(sum);
        }
    }
    return output;
}

// bar graph version
// specialized for kernel.size = 4
static std::vector<float>& apply_interp_filter_fma3_x4(const float *samples, size_t sz, const std::vector<int>& band_widths, const std::vector<float>& x, const Kernel<float>& kernel, std::vector<float>& output)
{
    assert(kernel.radius == 2);
    constexpr auto step = sizeof(__m128) / sizeof(float);
    const auto sse_stop = (intmax_t)sz - 2;
    const auto bands = (intmax_t)band_widths.size();
    if((intmax_t)output.size() < bands)
        output.resize(bands);
    for(intmax_t i = 0, k = 0, l = 0; i < bands; ++i)
    {
        auto vecsum = _mm_setzero_ps();
        auto count = (intmax_t)band_widths[i];
        for(intmax_t j = 0; j < count; ++j, ++k, l += step)
        {
            auto index = (intmax_t)x[k];
            if((index >= 1) && (index < sse_stop))
                vecsum = _mm_fmadd_ps(_mm_loadu_ps(&samples[index - 1]), _mm_load_ps(&kernel.weights[l]), vecsum);
            else
            {
                const auto start = index - 1;
                const auto stop = std::min(index + 3, (intmax_t)sz);

                auto sum = _mm_setzero_ps();
                for(auto m = std::max(start, (intmax_t)0); m < stop; ++m)
                    sum = _mm_fmadd_ss(_mm_load_ss(&samples[m]), _mm_load_ss(&kernel.weights[l + (m - start)]), sum);
                vecsum = _mm_add_ss(vecsum, sum);
            }
        }
        output[i] = horizontal_sum(vecsum) / count;
    }
    return output;
}

std::vector<float>& apply_interp_filter_fma3(const float *samples, size_t sz, const std::vector<float>& x, const Kernel<float>& kernel, std::vector<float>& output)
{
    if(kernel.size == 8)
        return apply_interp_filter_fma3_x8(samples, sz, x, kernel, output); // lanczos
    else if(kernel.size == 4)
        return apply_interp_filter_fma3_x4(samples, sz, x, kernel, output); // catmull-rom
    else
        return apply_interp_filter(samples, sz, x, kernel, output); // fallback
}

std::vector<float>& apply_interp_filter_fma3(const float *samples, size_t sz, const std::vector<int>& band_widths, const std::vector<float>& x, const Kernel<float>& kernel, std::vector<float>& output)
{
    if(kernel.size == 8)
        return apply_interp_filter_fma3_x8(samples, sz, band_widths, x, kernel, output); // lanczos
    else if(kernel.size == 4)
        return apply_interp_filter_fma3_x4(samples, sz, band_widths, x, kernel, output); // catmull-rom
    else
        return apply_interp_filter(samples, sz, band_widths, x, kernel, output); // fallback
}
