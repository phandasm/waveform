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
#include "filter.hpp"

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

enum class FilterMode
{
    NONE,
    GAUSS
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

enum class DisplayMode
{
    CURVE,
    BAR,
    STEPPED_BAR
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
    std::string m_audio_source_name;

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
    unsigned int m_height = 225;

    // show video source
    bool m_show = true;

    // graph was silent last frame
    bool m_last_silent = false;

    // audio capture retries
    int m_retries = 0;
    float m_next_retry = 0.0f;

    // settings
    RenderMode m_render_mode = RenderMode::SOLID;
    FFTWindow m_window_func = FFTWindow::HANN;
    InterpMode m_interp_mode = InterpMode::LANCZOS;
    FilterMode m_filter_mode = FilterMode::GAUSS;
    TSmoothingMode m_tsmoothing = TSmoothingMode::EXPONENTIAL;
    DisplayMode m_display_mode = DisplayMode::CURVE;
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
    bool m_log_scale = true;
    int m_bar_width = 0;
    int m_bar_gap = 0;
    int m_step_width = 0;
    int m_step_gap = 0;
    int m_num_bars = 0;

    // interpolation
    std::vector<float> m_interp_indices;
    std::vector<float> m_interp_bufs[2];

    // filter
    Kernel<float> m_kernel;
    float m_filter_radius = 0.0f;

    // slope
    AVXBufR m_slope_modifiers;

    void get_settings(obs_data_t *settings);

    void recapture_audio();
    void release_audio_capture();
    bool check_audio_capture(float seconds);
    void free_fft();

    void init_interp(unsigned int sz);

    void render_curve(gs_effect_t *effect);
    void render_bars(gs_effect_t *effect);

    // constants
    static const float DB_MIN;
    static constexpr auto RETRY_DELAY = 2.0f;

    inline float dbfs(float mag)
    {
        if(mag > 0.0f)
            return 20.0f * std::log10(mag);
        else
            return DB_MIN;
    }

public:
    WAVSource(obs_data_t *settings, obs_source_t *source);
    virtual ~WAVSource();

    // no copying
    WAVSource(const WAVSource&) = delete;
    WAVSource& operator=(const WAVSource&) = delete;

    unsigned int width();
    unsigned int height();

    // main callbacks
    virtual void update(obs_data_t *settings);
    virtual void tick(float seconds) = 0;
    virtual void render(gs_effect_t *effect);

    void show();
    void hide();

    static void register_source();

    // audio capture callback
    void capture_audio(obs_source_t *source, const audio_data *audio, bool muted);

    // constants
    static const bool HAVE_AVX2;
    static const bool HAVE_AVX;
    static const bool HAVE_SSE41;
    static const bool HAVE_FMA3;
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
