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
#include <immintrin.h>

static inline float horizontal_sum(__m128 vec)
{
    auto low = vec;
    auto high = _mm_permute_ps(low, _MM_SHUFFLE(3, 2, 3, 2)); // high[0] = low[2], high[1] = low[3]
    low = _mm_add_ps(high, low); // (h[0] + l[0]) (h[1] + l[1])
    high = _mm_movehdup_ps(low); // high[0] = low[1]
    return _mm_cvtss_f32(_mm_add_ss(high, low));
}

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
