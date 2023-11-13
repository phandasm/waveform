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
#include <cassert>

// portable non-SIMD implementation
// see comments of WAVSourceAVX2 and WAVSourceAVX
void WAVSourceGeneric::tick_spectrum([[maybe_unused]] float seconds)
{
    //std::lock_guard lock(m_mtx); // now locked in tick()

    const auto bufsz = m_fft_size * sizeof(float);
    const auto outsz = m_fft_size / 2;
    constexpr auto step = 1;

    const auto dtcapture = m_tick_ts - m_capture_ts;

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

    const int64_t dtaudio = get_audio_sync(m_tick_ts);
    const size_t dtsize = ((dtaudio > 0) ? size_t(ns_to_audio_frames(m_audio_info.samples_per_sec, (uint64_t)dtaudio)) * sizeof(float) : 0) + bufsz;
    auto silent_channels = 0u;
    for(auto channel = 0u; channel < m_capture_channels; ++channel)
    {
        if(m_capturebufs[channel].size >= dtsize)
        {
            circlebuf_pop_front(&m_capturebufs[channel], nullptr, m_capturebufs[channel].size - dtsize);
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

        const auto mag_coefficient = 2.0f / m_window_sum;
        const auto g = get_gravity(seconds);
        const auto g2 = 1.0f - g;
        const bool slope = m_slope > 0.0f;
        for(size_t i = 0; i < outsz; i += step)
        {
            auto real = m_fft_output[i][0];
            auto imag = m_fft_output[i][1];

            auto mag = std::hypot(real, imag) * mag_coefficient;

            if(slope)
                mag *= m_slope_modifiers[i];

            if(m_tsmoothing != TSmoothingMode::NONE)
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

    if(m_normalize_volume)
    {
        const auto volume_compensation = std::min(m_volume_target - dbfs(m_input_rms), m_max_gain);
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

void WAVSourceGeneric::tick_meter([[maybe_unused]] float seconds)
{
    const auto dtcapture = m_tick_ts - m_capture_ts;
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
    const int64_t dtaudio = get_audio_sync(m_tick_ts);
    const size_t dtsize = (dtaudio > 0) ? size_t(ns_to_audio_frames(m_audio_info.samples_per_sec, (uint64_t)dtaudio)) * sizeof(float) : 0;

    for(auto channel = 0u; channel < m_capture_channels; ++channel)
    {
        while(m_capturebufs[channel].size > dtsize)
        {
            auto consume = m_capturebufs[channel].size - dtsize;
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
    {
        for(auto& i : m_meter_buf)
            i = 0.0f;
        for(auto& i : m_meter_val)
            i = DB_MIN;
        m_last_silent = true;
        return;
    }

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

        if(m_tsmoothing != TSmoothingMode::NONE)
        {
            const auto g = get_gravity(seconds);
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

void WAVSourceGeneric::tick_waveform([[maybe_unused]] float seconds)
{
    // TODO: optimization
    const auto bufsz = m_fft_size * sizeof(float);
    const auto outsz = m_fft_size;
    constexpr auto step = 1;

    const auto dtcapture = m_tick_ts - m_capture_ts;

    if(!m_show || (dtcapture > CAPTURE_TIMEOUT))
    {
        if(m_last_silent)
            return;
        for(auto channel = 0; channel < (m_stereo ? 2 : 1); ++channel)
            for(size_t i = 0; i < outsz; ++i)
                m_decibels[channel][i] = DB_MIN;
        m_last_silent = true;
        return;
    }

    const int64_t dtaudio = get_audio_sync(m_tick_ts);
    const size_t dtsize = ((dtaudio > 0) ? size_t(ns_to_audio_frames(m_audio_info.samples_per_sec, (uint64_t)dtaudio)) * sizeof(float) : 0) + bufsz;
    for(auto i = 0u; i < m_capture_channels; ++i)
        if(m_capturebufs[i].size < dtsize) // check if we have enough audio in advance
            return;


    auto silent_channels = 0u;
    for(auto channel = 0u; channel < m_capture_channels; ++channel)
    {
        circlebuf_pop_front(&m_capturebufs[channel], nullptr, m_capturebufs[channel].size - dtsize);
        circlebuf_peek_front(&m_capturebufs[channel], m_decibels[channel].get(), bufsz);

        bool silent = true;
        for(auto i = 0u; i < m_fft_size; i += step)
        {
            if(m_decibels[channel][i] != 0.0f)
            {
                silent = false;
                m_last_silent = false;
                break;
            }
        }

        if(silent)
        {
            if(++silent_channels >= m_capture_channels)
                m_last_silent = true;
        }
    }

    if(m_last_silent)
    {
        for(auto channel = 0; channel < (m_stereo ? 2 : 1); ++channel)
            for(size_t i = 0; i < outsz; ++i)
                m_decibels[channel][i] = DB_MIN;
        return;
    }

    if(m_output_channels > m_capture_channels)
        memcpy(m_decibels[1].get(), m_decibels[0].get(), outsz * sizeof(float));

    if(m_stereo)
    {
        for(auto channel = 0; channel < 2; ++channel)
            for(size_t i = 0; i < outsz; ++i)
                m_decibels[channel][i] = dbfs(std::abs(m_decibels[channel][i]));
    }
    else if(m_capture_channels > 1)
    {
        for(size_t i = 0; i < outsz; ++i)
            m_decibels[0][i] = dbfs((std::abs(m_decibels[0][i]) + std::abs(m_decibels[1][i])) * 0.5f);
    }
    else
    {
        for(size_t i = 0; i < outsz; ++i)
            m_decibels[0][i] = dbfs(std::abs(m_decibels[0][i]));
    }

    if(m_normalize_volume)
    {
        const auto volume_compensation = std::min(m_volume_target - dbfs(m_input_rms), m_max_gain);
        for(auto channel = 0; channel < (m_stereo ? 2 : 1); ++channel)
            for(size_t i = 1; i < outsz; ++i)
                m_decibels[channel][i] += volume_compensation;
    }
}

void WAVSourceGeneric::update_input_rms()
{
    assert(m_normalize_volume);

    if(!sync_rms_buffer())
        return;

    float sum = 0.0f;
    for(size_t i = 0; i < m_input_rms_size; ++i)
        sum += m_input_rms_buf[i];
    m_input_rms = std::sqrt(sum / m_input_rms_size);
}
