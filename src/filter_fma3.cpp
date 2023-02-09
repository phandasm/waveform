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

std::vector<float>& apply_lanczos_filter_fma3(const float *samples, size_t sz, const std::vector<float>& x, const Kernel<float>& kernel, std::vector<float>& output)
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
            output[i] = lanczos_convolve(samples, sz, kernel, index, j);
    }
    return output;
}

// bar graph version
std::vector<float>& apply_lanczos_filter_fma3(const float *samples, size_t sz, const std::vector<int>& band_widths, const std::vector<float>& x, const Kernel<float>& kernel, std::vector<float>& output)
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
        auto fsum = 0.0f;
        auto count = (intmax_t)band_widths[i];
        for(intmax_t j = 0; j < count; ++j, ++k, l += step)
        {
            auto index = (intmax_t)x[k];
            if((index >= 3) && (index < avx_stop))
                vecsum = _mm256_fmadd_ps(_mm256_loadu_ps(&samples[index - 3]), _mm256_load_ps(&kernel.weights[l]), vecsum);
            else
                fsum += lanczos_convolve(samples, sz, kernel, index, l);
        }
        output[i] = (fsum + horizontal_sum(vecsum)) / count;
    }
    return output;
}
