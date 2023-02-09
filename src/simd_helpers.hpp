/*
    Copyright (C) 2023 Devin Davila

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

#ifdef __AVX__

#include <immintrin.h>

static WAV_FORCE_INLINE float horizontal_sum(__m128 vec)
{
    auto low = vec;
    auto high = _mm_permute_ps(low, _MM_SHUFFLE(3, 2, 3, 2)); // high[0] = low[2], high[1] = low[3]
    low = _mm_add_ps(high, low); // (h[0] + l[0]) (h[1] + l[1])
    high = _mm_movehdup_ps(low); // high[0] = low[1]
    return _mm_cvtss_f32(_mm_add_ss(high, low));
}

static WAV_FORCE_INLINE float horizontal_sum(__m256 vec)
{
    return horizontal_sum(_mm_add_ps(_mm256_extractf128_ps(vec, 1), _mm256_castps256_ps128(vec)));
}

static WAV_FORCE_INLINE float horizontal_max(__m256 vec)
{
    auto high = _mm256_extractf128_ps(vec, 1); // split into two 128-bit vecs
    auto low = _mm_max_ps(high, _mm256_castps256_ps128(vec)); // max(h[0], l[0]) max(h[1], l[1]) max(h[2], l[2]) max(h[3], l[3])
    high = _mm_permute_ps(low, _MM_SHUFFLE(3, 2, 3, 2)); // high[0] = low[2], high[1] = low[3]
    low = _mm_max_ps(high, low); // max(h[0], l[0]) max(h[1], l[1])
    high = _mm_movehdup_ps(low); // high[0] = low[1]
    return _mm_cvtss_f32(_mm_max_ss(high, low));
}

#endif // __AVX__
