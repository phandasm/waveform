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
#include <algorithm>
#include <cstring>
#include <cmath>
#include <util/platform.h>

// portable non-SIMD implementation
// see comments of WAVSourceAVX2 and WAVSourceAVX
void WAVSourceGeneric::tick_spectrum(float seconds)
{
    //std::lock_guard lock(m_mtx); // now locked in tick()
    if(!check_audio_capture(seconds))
        return;

    if(m_capture_channels == 0)
        return;

    const auto bufsz = m_fft_size * sizeof(float);
    const auto outsz = m_fft_size / 2;
    constexpr auto step = 1;

    const auto dtcapture = os_gettime_ns() - m_capture_ts;

    if(!m_show || (dtcapture > CAPTURE_TIMEOUT))
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
        if(m_capturebufs[channel].size >= bufsz)
        {
            circlebuf_pop_front(&m_capturebufs[channel], nullptr, std::min(dtsize, m_capturebufs[channel].size - bufsz));
            circlebuf_peek_front(&m_capturebufs[channel], m_fft_input.get(), bufsz);
        }
        else
            continue;

        bool silent = true;
        for(auto i = 0u; i < m_fft_size; i += step)
        {
            if(m_fft_input[i] != 0.0f)
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
            auto floor = (float)(m_floor - 10);
            for(size_t i = 0; i < outsz; i += step)
            {
                const auto ch = (m_stereo) ? channel : 0u;
                if(m_decibels[ch][i] > floor)
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
                inbuf[i] *= mulbuf[i];
        }

        if(m_fft_plan != nullptr)
            fftwf_execute(m_fft_plan);
        else
            continue;

        const auto mag_coefficient = 2.0f / (float)m_fft_size;
        const auto g = m_gravity;
        const auto g2 = 1.0f - g;
        const bool slope = m_slope > 0.0f;
        for(size_t i = 0; i < outsz; i += step)
        {
            auto real = m_fft_output[i][0];
            auto imag = m_fft_output[i][1];

            auto mag = std::sqrt((real * real) + (imag * imag)) * mag_coefficient;

            if(slope)
                mag *= m_slope_modifiers[i];

            if(m_tsmoothing == TSmoothingMode::EXPONENTIAL)
            {
                auto oldval = m_tsmooth_buf[channel][i];
                if(m_fast_peaks)
                    oldval = std::max(mag, oldval);

                mag = (g * oldval) + (g2 * mag);
                m_tsmooth_buf[channel][i] = mag;
            }

            m_decibels[channel][i] = mag;
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

    if(m_normalize_volume && !m_last_silent)
    {
        const auto volume_compensation = std::min(-3.0f - dbfs(m_input_rms), 30.0f);
        for(auto channel = 0; channel < (m_stereo ? 2 : 1); ++channel)
            for(size_t i = 1; i < outsz; ++i)
                m_decibels[channel][i] += volume_compensation;
    }

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

void WAVSourceGeneric::tick_meter(float seconds)
{
    if(!check_audio_capture(seconds))
        return;

    if(m_capture_channels == 0)
        return;

    const auto dtcapture = os_gettime_ns() - m_capture_ts;
    if(dtcapture > CAPTURE_TIMEOUT)
    {
        if(m_last_silent)
            return;
        for(auto channel = 0u; channel < m_capture_channels; ++channel)
            for(size_t i = 0u; i < m_fft_size; ++i)
                m_decibels[channel][i] = 0.0f;

        for(auto& i : m_meter_buf)
            i = 0.0f;
        for(auto& i : m_meter_val)
            i = DB_MIN;
        m_last_silent = true;
        return;
    }

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
        float out = 0.0f;
        if(m_meter_rms)
        {
            for(size_t i = 0; i < outsz; ++i)
            {
                auto val = m_decibels[channel][i];
                out += val * val;
            }
            out = std::sqrt(out / m_fft_size);
        }
        else
        {
            for(size_t i = 0; i < outsz; ++i)
                out = std::max(out, std::abs(m_decibels[channel][i]));
        }

        if(m_tsmoothing == TSmoothingMode::EXPONENTIAL)
        {
            const auto g = m_gravity;
            const auto g2 = 1.0f - g;
            if(!m_fast_peaks || (out <= m_meter_buf[channel]))
                out = (g * m_meter_buf[channel]) + (g2 * out);
        }
        m_meter_buf[channel] = out;
        m_meter_val[channel] = dbfs(out);
    }

    auto silent_channels = 0u;
    for(auto channel = 0u; channel < m_capture_channels; ++channel)
        if(m_meter_val[channel] < (m_floor - 10))
            ++silent_channels;

    m_last_silent = (silent_channels >= m_capture_channels);
}

void WAVSourceGeneric::update_input_rms(const audio_data *audio)
{
    if((audio == nullptr) || (m_capture_channels == 0))
        return;
    const auto sz = audio->frames;
    auto data = (float**)&audio->data;
    if(m_capture_channels > 1)
    {
        if((data[0] == nullptr) || (data[1] == nullptr))
            return;
        for(auto i = 0u; i < sz; ++i)
        {
            auto val = std::max(std::abs(data[0][i]), std::abs(data[1][i]));
            m_input_rms_buf[m_input_rms_pos++] = val * val;
            if(m_input_rms_pos >= m_input_rms_size)
                m_input_rms_pos = 0;
        }
    }
    else
    {
        if(data[0] == nullptr)
            return;
        for(auto i = 0u; i < sz; ++i)
        {
            auto val = data[0][i];
            m_input_rms_buf[m_input_rms_pos++] = val * val;
            if(m_input_rms_pos >= m_input_rms_size)
                m_input_rms_pos = 0;
        }
    }

    float sum = 0.0f;
    for(size_t i = 0; i < m_input_rms_size; ++i)
        sum += m_input_rms_buf[i];
    m_input_rms = std::sqrt(sum / m_input_rms_size);
}
