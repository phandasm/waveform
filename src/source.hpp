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
#include <mutex>
#include <obs-module.h>
#include <util/circlebuf.h>
#include <fftw3.h>
#include <memory>
#include "module.hpp"
#include "aligned_mem.hpp"

using AVXBufR = std::unique_ptr<float[], AVXDeleter>;
using AVXBufC = std::unique_ptr<fftwf_complex[], AVXDeleter>;

enum class FFTWindow
{
    NONE,
    HANN,
    HAMMING,
    BLACKMAN,
    BLACKMAN_HARRIS
};

enum class InterpMode
{
    POINT,
    LANCZOS
};

// temporal smoothing
enum class TSmoothingMode
{
    NONE,
    EXPONENTIAL
};

enum class RenderMode
{
    LINE,
    SOLID,
    GRADIENT
};

class WAVSource
{
protected:
    // audio callback (and posssibly others) run in separate thread
    // obs_source_remove_audio_capture_callback evidently flushes the callback
    // so mutex must be recursive to avoid deadlock
    std::recursive_timed_mutex m_mtx;

    // obs sources
    obs_source_t *m_source = nullptr;               // our source
    obs_weak_source_t *m_audio_source = nullptr;    // captured audio source

    // audio capture
    obs_audio_info m_audio_info{};
    circlebuf m_capturebufs[2]{};
    uint32_t m_capture_channels = 0;    // audio input channels
    uint32_t m_output_channels = 0;     // fft output channels (*not* display channels)

    // 32-byte aligned buffers for FFT/AVX processing
    AVXBufR m_fft_input;
    AVXBufC m_fft_output;
    fftwf_plan m_fft_plan{};
    AVXBufR m_window_coefficients;
    AVXBufR m_tsmooth_buf[2];   // last frames magnitudes
    AVXBufR m_decibels[2];      // dBFS
    size_t m_fft_size = 0;      // number of fft elements (not bytes, multiple of 16)

    // video fps
    double m_fps = 0.0;

    // video size
    unsigned int m_width = 800;
    unsigned int m_height = 600;

    // show video source
    bool m_show = true;

    // graph was silent last frame
    bool m_last_silent = false;

    // settings
    RenderMode m_render_mode = RenderMode::SOLID;
    FFTWindow m_window_func = FFTWindow::HANN;
    InterpMode m_interp_mode = InterpMode::LANCZOS;
    TSmoothingMode m_tsmoothing = TSmoothingMode::EXPONENTIAL;
    bool m_stereo = false;
    bool m_auto_fft_size = true;
    int m_cutoff_low = 0;
    int m_cutoff_high = 24000;
    int m_floor = -120;
    int m_ceiling = 0;
    float m_gravity = 0.0f;
    float m_grad_ratio = 1.0f;
    bool m_fast_peaks = false;
    vec4 m_color_base{ 1.0, 1.0, 1.0, 1.0 };
    vec4 m_color_crest{ 1.0, 1.0, 1.0, 1.0 };
    float m_slope = 0.0f;

    void get_settings(obs_data_t *settings);

    void recapture_audio(obs_data_t *settings);
    void release_audio_capture();
    void free_fft();

public:
    WAVSource(obs_data_t *settings, obs_source_t *source);
    virtual ~WAVSource();

    // no copying
    WAVSource(const WAVSource&) = delete;
    WAVSource& operator=(const WAVSource&) = delete;

    constexpr unsigned int width() { return m_width; }
    constexpr unsigned int height() { return m_height; }

    // main callbacks
    virtual void update(obs_data_t *settings);
    virtual void tick(float seconds) = 0;
    virtual void render(gs_effect_t *effect);

    constexpr void show() { m_show = true; }
    constexpr void hide() { m_show = false; }

    static void register_source();

    // audio capture callback
    void capture_audio(obs_source_t *source, const audio_data *audio, bool muted);
};

class WAVSourceAVX2 : public WAVSource
{
public:
    using WAVSource::WAVSource;
    ~WAVSourceAVX2() override {}

    void tick(float seconds) override;
};

class WAVSourceAVX : public WAVSource
{
public:
    using WAVSource::WAVSource;
    ~WAVSourceAVX() override {}

    void tick(float seconds) override;
};

class WAVSourceSSE2 : public WAVSource
{
public:
    using WAVSource::WAVSource;
    ~WAVSourceSSE2() override {}

    void tick(float seconds) override;
};
