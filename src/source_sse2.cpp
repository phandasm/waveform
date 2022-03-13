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

#include "waveform_config.hpp"
#include "source.hpp"
#include <immintrin.h>
#include <algorithm>
#include <cstring>

// compatibility fallback using at most SSE2 instructions
// see comments of WAVSourceAVX2
DECORATE_SSE2
void WAVSourceSSE2::tick_spectrum(float seconds)
{
    //std::lock_guard lock(m_mtx); // now locked in tick()
    if(!check_audio_capture(seconds))
        return;

    if(m_capture_channels == 0)
        return;

    const auto bufsz = m_fft_size * sizeof(float);
    const auto outsz = m_fft_size / 2;
    constexpr auto step = sizeof(__m128) / sizeof(float);

    if(!m_show)
    {
        if(m_last_silent)
            return;
        for(auto channel = 0u; channel < m_capture_channels; ++channel)
            memset(m_tsmooth_buf[channel].get(), 0, outsz * sizeof(float));
        for(auto channel = 0; channel < (m_stereo ? 2 : 1); ++channel)
            for(size_t i = 0; i < outsz; ++i)
                m_decibels[channel][i] = DB_MIN;
        m_last_silent = true;
        return;
    }

    auto silent_channels = 0u;
    for(auto channel = 0u; channel < m_capture_channels; ++channel)
    {
        if(m_capturebufs[channel].size >= bufsz)
        {
            circlebuf_peek_front(&m_capturebufs[channel], m_fft_input.get(), bufsz);
            circlebuf_pop_front(&m_capturebufs[channel], nullptr, m_capturebufs[channel].size - bufsz);
        }
        else
            continue;

        bool silent = true;
        const auto zero = _mm_setzero_ps();
        for(auto i = 0u; i < m_fft_size; i += step)
        {
            auto mask = _mm_cmpeq_ps(zero, _mm_load_ps(&m_fft_input[i]));
            if(_mm_movemask_ps(mask) != 0xf)
            {
                silent = false;
                m_last_silent = false;
                break;
            }
        }

        if(silent)
        {
            if(m_last_silent)
                continue;
            bool outsilent = true;
            auto floor = _mm_set1_ps((float)(m_floor - 10));
            for(size_t i = 0; i < outsz; i += step)
            {
                const auto ch = (m_stereo) ? channel : 0u;
                auto mask = _mm_cmpgt_ps(floor, _mm_load_ps(&m_decibels[ch][i]));
                if(_mm_movemask_ps(mask) != 0xf)
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

        if(m_window_func != FFTWindow::NONE)
        {
            auto inbuf = m_fft_input.get();
            auto mulbuf = m_window_coefficients.get();
            for(auto i = 0u; i < m_fft_size; i += step)
                _mm_store_ps(&inbuf[i], _mm_mul_ps(_mm_load_ps(&inbuf[i]), _mm_load_ps(&mulbuf[i])));
        }

        if(m_fft_plan != nullptr)
            fftwf_execute(m_fft_plan);
        else
            continue;

        constexpr auto shuffle_mask_r = 0 | (2 << 2) | (0 << 4) | (2 << 6);
        constexpr auto shuffle_mask_i = 1 | (3 << 2) | (1 << 4) | (3 << 6);
        const auto mag_coefficient = _mm_div_ps(_mm_set1_ps(2.0f), _mm_set1_ps((float)m_fft_size));
        const auto g = _mm_set1_ps(m_gravity);
        const auto g2 = _mm_sub_ps(_mm_set1_ps(1.0), g);
        const bool slope = m_slope > 0.0f;
        for(size_t i = 0; i < outsz; i += step)
        {
            // load 4 real/imaginary pairs and pack the r/i components into separate vectors
            const float *buf = &m_fft_output[i][0];
            auto chunk1 = _mm_load_ps(buf);
            auto chunk2 = _mm_load_ps(&buf[4]);
            auto rvec = _mm_shuffle_ps(chunk1, chunk2, shuffle_mask_r);
            auto ivec = _mm_shuffle_ps(chunk1, chunk2, shuffle_mask_i);

            auto mag = _mm_sqrt_ps(_mm_add_ps(_mm_mul_ps(ivec, ivec), _mm_mul_ps(rvec, rvec)));
            mag = _mm_mul_ps(mag, mag_coefficient);

            if(slope)
                mag = _mm_mul_ps(mag, _mm_load_ps(&m_slope_modifiers[i]));

            if(m_tsmoothing == TSmoothingMode::EXPONENTIAL)
            {
                if(m_fast_peaks)
                    _mm_store_ps(&m_tsmooth_buf[channel][i], _mm_max_ps(mag, _mm_load_ps(&m_tsmooth_buf[channel][i])));

                mag = _mm_add_ps(_mm_mul_ps(g, _mm_load_ps(&m_tsmooth_buf[channel][i])), _mm_mul_ps(g2, mag));
                _mm_store_ps(&m_tsmooth_buf[channel][i], mag);
            }

            _mm_store_ps(&m_decibels[channel][i], mag);
        }
    }

    if(m_last_silent)
        return;

    if(m_output_channels > m_capture_channels)
        memcpy(m_decibels[1].get(), m_decibels[0].get(), outsz * sizeof(float));

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
}

void WAVSourceSSE2::tick_meter(float seconds)
{
    if(!check_audio_capture(seconds))
        return;

    if(m_capture_channels == 0)
        return;

    const auto outsz = m_fft_size;

    for(auto channel = 0u; channel < m_capture_channels; ++channel)
    {
        while(m_capturebufs[channel].size > 0)
        {
            auto consume = m_capturebufs[channel].size;
            auto max = (m_fft_size - m_meter_pos[channel]) * sizeof(float);
            if(consume >= max)
            {
                circlebuf_pop_front(&m_capturebufs[channel], &m_decibels[channel][m_meter_pos[channel]], max);
                m_meter_pos[channel] = 0;
            }
            else
            {
                circlebuf_pop_front(&m_capturebufs[channel], &m_decibels[channel][m_meter_pos[channel]], consume);
                m_meter_pos[channel] += consume / sizeof(float);
            }
        }
    }

    if(!m_show)
        return;

    for(auto channel = 0u; channel < m_capture_channels; ++channel)
    {
        if(m_meter_rms)
        {
            float sum = 0.0f;
            for(size_t i = 0; i < outsz; ++i)
            {
                auto val = m_decibels[channel][i];
                sum += val * val;
            }
            const auto g = m_gravity;
            const auto g2 = 1.0f - g;
            auto rms = std::sqrt(sum / m_fft_size);
            if(m_tsmoothing == TSmoothingMode::EXPONENTIAL)
            {
                if(!m_fast_peaks || (rms <= m_meter_buf[channel]))
                    rms = (g * m_meter_buf[channel]) + (g2 * rms);
            }
            m_meter_buf[channel] = rms;
            m_meter_val[channel] = dbfs(rms);
        }
        else
        {
            const auto g = m_gravity;
            const auto g2 = 1.0f - g;
            float max = 0.0f;
            for(size_t i = 0; i < outsz; ++i)
            {
                auto val = std::abs(m_decibels[channel][i]);
                if(val > max)
                    max = val;
            }
            if(m_tsmoothing == TSmoothingMode::EXPONENTIAL)
            {
                if(!m_fast_peaks || (max <= m_meter_buf[channel]))
                    max = (g * m_meter_buf[channel]) + (g2 * max);
            }
            m_meter_buf[channel] = max;
            m_meter_val[channel] = dbfs(max);
        }
    }
}
