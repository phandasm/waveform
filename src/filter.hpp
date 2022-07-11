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
#include "membuf.hpp"
#include <cmath>
#include <cstdint>
#include <vector>
#include <type_traits>
#include <memory>

template<typename T>
struct Kernel
{
    std::unique_ptr<T[], MembufDeleter> weights;
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
    ret.weights.reset(membuf_alloc<T>(size));
    ret.radius = w;
    ret.size = size;
    ret.sse_size = size & -(16 / (int)sizeof(T));
    constexpr auto pi2 = (T)M_PI * (T)2;
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
std::vector<T>& apply_filter(const std::vector<T>& samples, const Kernel<T>& kernel, std::vector<T>& output)
{
    const auto sz = samples.size();
    if(output.size() < sz)
        output.resize(sz);
    for(auto i = 0u; i < sz; ++i)
        output[i] = weighted_avg(samples, kernel, i);
    return output;
}

#ifndef DISABLE_X86_SIMD

float weighted_avg_fma3(const std::vector<float>& samples, const Kernel<float>& kernel, intmax_t index);
//double weighted_avg_fma3(const std::vector<double>& samples, const Kernel<double>& kernel, intmax_t index);

std::vector<float>& apply_filter_fma3(const std::vector<float>& samples, const Kernel<float>& kernel, std::vector<float>& output);
//std::vector<double>& apply_filter_fma3(const std::vector<double>& samples, const Kernel<double>& kernel, std::vector<double>& output);

#endif // !DISABLE_X86_SIMD
