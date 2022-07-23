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

#include "source.hpp"
#include <immintrin.h>
#include <algorithm>
#include <cstring>

void WAVSourceAVX2::tick_spectrum(float seconds)
{
    //std::lock_guard lock(m_mtx); // now locked in tick()
    if(!check_audio_capture(seconds))
        return;

    if(m_capture_channels == 0)
        return;

    const auto bufsz = m_fft_size * sizeof(float);
    const auto outsz = m_fft_size / 2; // discard bins at nyquist and above
    constexpr auto step = sizeof(__m256) / sizeof(float);

    // reset and stop processing when source is not being displayed
    if(!m_show)
    {
        if(m_last_silent)
            return;
        for(auto channel = 0u; channel < m_capture_channels; ++channel)
            if(m_tsmooth_buf[channel] != nullptr)
                memset(m_tsmooth_buf[channel].get(), 0, outsz * sizeof(float));
        for(auto channel = 0; channel < (m_stereo ? 2 : 1); ++channel)
            for(size_t i = 0; i < outsz; ++i)
                m_decibels[channel][i] = DB_MIN;
        m_last_silent = true;
        return;
    }

    const auto dtsize = size_t(seconds * m_audio_info.samples_per_sec) * sizeof(float);
    auto silent_channels = 0u;
    for(auto channel = 0u; channel < m_capture_channels; ++channel)
    {
        // get captured audio
        if(m_capturebufs[channel].size >= bufsz)
        {
            circlebuf_pop_front(&m_capturebufs[channel], nullptr, std::min(dtsize, m_capturebufs[channel].size - bufsz));
            circlebuf_peek_front(&m_capturebufs[channel], m_fft_input.get(), bufsz);
        }
        else
            continue;

        // skip FFT for silent audio
        bool silent = true;
        const auto zero = _mm256_setzero_ps();
        for(auto i = 0u; i < m_fft_size; i += step)
        {
            auto mask = _mm256_cmp_ps(zero, _mm256_load_ps(&m_fft_input[i]), _CMP_EQ_OQ);
            if(_mm256_movemask_ps(mask) != 0xff)
            {
                silent = false;
                m_last_silent = false;
                break;
            }
        }

        // wait for gravity
        if(silent)
        {
            if(m_last_silent)
                continue;
            bool outsilent = true;
            auto floor = _mm256_set1_ps((float)(m_floor - 10));
            for(size_t i = 0; i < outsz; i += step)
            {
                const auto ch = (m_stereo) ? channel : 0u;
                auto mask = _mm256_cmp_ps(floor, _mm256_load_ps(&m_decibels[ch][i]), _CMP_GT_OQ);
                if(_mm256_movemask_ps(mask) != 0xff)
                {
                    outsilent = false;
                    break;
                }
            }
            if(outsilent)
            {
                if(++silent_channels >= m_capture_channels)
                    m_last_silent = true;
                continue;
            }
        }

        // window function
        if(m_window_func != FFTWindow::NONE)
        {
            auto inbuf = m_fft_input.get();
            auto mulbuf = m_window_coefficients.get();
            for(auto i = 0u; i < m_fft_size; i += step)
                _mm256_store_ps(&inbuf[i], _mm256_mul_ps(_mm256_load_ps(&inbuf[i]), _mm256_load_ps(&mulbuf[i])));
        }

        // FFT
        if(m_fft_plan != nullptr)
            fftwf_execute(m_fft_plan);
        else
            continue;

        // normalize FFT output and convert to dBFS
        const auto shuffle_mask = _mm256_setr_epi32(0, 2, 4, 6, 1, 3, 5, 7);
        const auto mag_coefficient = _mm256_div_ps(_mm256_set1_ps(2.0f), _mm256_set1_ps((float)m_fft_size));
        const auto g = _mm256_set1_ps(m_gravity);
        const auto g2 = _mm256_sub_ps(_mm256_set1_ps(1.0), g); // 1 - gravity
        const bool slope = m_slope > 0.0f;
        for(size_t i = 0; i < outsz; i += step)
        {
            // this *should* be faster than 2x vgatherxxx instructions
            // load 8 real/imaginary pairs and group the r/i components in the low/high halves
            const float *buf = &m_fft_output[i][0]; // first element of complex (float[2])
            auto chunk1 = _mm256_permutevar8x32_ps(_mm256_load_ps(buf), shuffle_mask);
            auto chunk2 = _mm256_permutevar8x32_ps(_mm256_load_ps(&buf[step]), shuffle_mask);

            // pack the real and imaginary components into separate vectors
            auto rvec = _mm256_insertf128_ps(chunk1, _mm256_castps256_ps128(chunk2), 1); // faster than vperm2f128 on AMD until Zen2
            auto ivec = _mm256_permute2f128_ps(chunk1, chunk2, 1 | (3 << 4)); // no choice here (without using more instructions)

            // calculate normalized magnitude
            // 2 * magnitude / N
            auto mag = _mm256_sqrt_ps(_mm256_fmadd_ps(ivec, ivec, _mm256_mul_ps(rvec, rvec))); // magnitude sqrt(r^2 + i^2)
            mag = _mm256_mul_ps(mag, mag_coefficient); // 2 * magnitude / N with precomputed quotient

            // boost high frequencies
            if(slope)
                mag = _mm256_mul_ps(mag, _mm256_load_ps(&m_slope_modifiers[i]));

            // time domain smoothing
            if(m_tsmoothing == TSmoothingMode::EXPONENTIAL)
            {
                // take new values immediately if larger
                if(m_fast_peaks)
                    _mm256_store_ps(&m_tsmooth_buf[channel][i], _mm256_max_ps(mag, _mm256_load_ps(&m_tsmooth_buf[channel][i])));

                // (gravity * oldval) + ((1 - gravity) * newval)
                mag = _mm256_fmadd_ps(g, _mm256_load_ps(&m_tsmooth_buf[channel][i]), _mm256_mul_ps(g2, mag));
                _mm256_store_ps(&m_tsmooth_buf[channel][i], mag);
            }

            _mm256_store_ps(&m_decibels[channel][i], mag); // end of the line for AVX
        }
    }

    if(m_last_silent)
        return;

    if(m_output_channels > m_capture_channels)
        memcpy(m_decibels[1].get(), m_decibels[0].get(), outsz * sizeof(float));

    // dBFS conversion
    // 20 * log(2 * magnitude / N)
    if(m_stereo)
    {
        for(auto channel = 0; channel < 2; ++channel)
            for(size_t i = 0; i < outsz; ++i)
                m_decibels[channel][i] = dbfs(m_decibels[channel][i]);
    }
    else if(m_capture_channels > 1)
    {
        for(size_t i = 0; i < outsz; ++i)
            m_decibels[0][i] = dbfs((m_decibels[0][i] + m_decibels[1][i]) * 0.5f);
    }
    else
    {
        for(size_t i = 0; i < outsz; ++i)
            m_decibels[0][i] = dbfs(m_decibels[0][i]);
    }

    // roll-off
    if((m_rolloff_q > 0.0f) && (m_rolloff_rate > 0.0f))
    {
        for(auto channel = 0; channel < (m_stereo ? 2 : 1); ++channel)
        {
            for(size_t i = 1; i < outsz; ++i)
            {
                auto val = m_decibels[channel][i] - m_rolloff_modifiers[i];
                m_decibels[channel][i] = std::max(val, DB_MIN);
            }
        }
    }
}
