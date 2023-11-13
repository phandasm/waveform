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
#include <graphics/vec3.h>
#include <graphics/vec4.h>
#include <fftw3.h>
#include "module.hpp"
#include "aligned_buffer.hpp"
#include "filter.hpp"

using AVXBufR = AlignedBuffer<float>;
using AVXBufC = AlignedBuffer<fftwf_complex>;

enum class FFTWindow
{
    NONE,
    HANN,
    HAMMING,
    BLACKMAN,
    BLACKMAN_HARRIS,
    POWER_OF_SINE
};

enum class InterpMode
{
    POINT,
    LANCZOS,
    CATROM
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
    EXPONENTIAL,
    TVEXPONENTIAL
};

enum class RenderMode
{
    LINE,
    SOLID,
    GRADIENT,
    PULSE,
    RANGE
};

enum class PulseMode
{
    MAGNITUDE,
    FREQUENCY
};

enum class DisplayMode
{
    CURVE,
    BAR,
    STEPPED_BAR,
    METER,
    STEPPED_METER,
    WAVEFORM
};

enum class ChannelMode
{
    MONO,
    STEREO,
    SINGLE
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
    uint32_t m_capture_channels = 0;        // audio input channels
    uint32_t m_output_channels = 0;         // fft output channels (*not* display channels)
    bool m_output_bus_captured = false;     // do we have an active audio output callback? (via audio_output_connect())

    // 32-byte aligned buffers for FFT/AVX processing
    AVXBufR m_fft_input;
    AVXBufC m_fft_output;
    fftwf_plan m_fft_plan{};
    AVXBufR m_window_coefficients;
    AVXBufR m_tsmooth_buf[2];               // last frames magnitudes
    AVXBufR m_decibels[2];                  // dBFS, or audio sample buffer in meter mode
    size_t m_fft_size = 0;                  // number of fft elements, or audio samples in meter/waveform mode (not bytes, multiple of 16)
                                            // in meter/waveform mode m_fft_size is the size of the circular buffer in samples

    // meter mode
    size_t m_meter_pos[2] = { 0, 0 };       // circular buffer position (per channel)
    float m_meter_val[2] = { 0.0f, 0.0f };  // dBFS
    float m_meter_buf[2] = { 0.0f, 0.0f };  // EMA
    bool m_meter_rms = false;               // RMS mode
    bool m_meter_mode = false;              // either meter or stepped meter display mode is selected
    int m_meter_ms = 100;                   // milliseconds of audio data to buffer

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

    uint64_t m_capture_ts = 0;  // timestamp of last audio callback in nanoseconds
    uint64_t m_audio_ts = 0;    // timestamp of the end of available audio in nanoseconds
    uint64_t m_tick_ts = 0;     // timestamp of last 'tick' in nanoseconds

    // settings
    RenderMode m_render_mode = RenderMode::SOLID;
    PulseMode m_pulse_mode = PulseMode::MAGNITUDE;
    FFTWindow m_window_func = FFTWindow::HANN;
    InterpMode m_interp_mode = InterpMode::LANCZOS;
    FilterMode m_filter_mode = FilterMode::GAUSS;
    TSmoothingMode m_tsmoothing = TSmoothingMode::EXPONENTIAL;
    DisplayMode m_display_mode = DisplayMode::CURVE;
    ChannelMode m_channel_mode = ChannelMode::MONO;
    bool m_stereo = false;
    bool m_auto_fft_size = true;
    int m_cutoff_low = 0;
    int m_cutoff_high = 24000;
    int m_floor = -120;
    int m_ceiling = 0;
    float m_gravity = 0.0f;
    float m_grad_ratio = 1.0f;
    int m_range_middle = -20;
    int m_range_crest = -9;
    bool m_fast_peaks = false;
    vec4 m_color_base{ {{1.0, 1.0, 1.0, 1.0}} };
    vec4 m_color_middle{ {{1.0, 1.0, 1.0, 1.0}} };
    vec4 m_color_crest{ {{1.0, 1.0, 1.0, 1.0}} };
    float m_slope = 0.0f;
    bool m_log_scale = true;
    bool m_mirror_freq_axis = false;
    int m_bar_width = 0;
    int m_bar_gap = 0;
    int m_step_width = 0;
    int m_step_gap = 0;
    int m_num_bars = 0;
    bool m_radial = false;
    bool m_invert = false;
    float m_deadzone = 0.0f; // radial display deadzone
    float m_radial_arc = 1.0f;
    float m_radial_rotation = 0.0f;
    bool m_rounded_caps = false;
    bool m_hide_on_silent = false;
    int m_channel_spacing = 0;
    float m_rolloff_q = 0.0f;
    float m_rolloff_rate = 0.0f;
    bool m_normalize_volume = false;
    float m_volume_target = -3.0f;  // volume normalization target
    float m_max_gain = 30.0f;       // maximum volume normalization gain
    int m_min_bar_height = 0;
    int m_channel_base = 0; // channel to use in single channel mode
    bool m_ignore_mute = false;
    int m_sine_exponent = 2;

    // interpolation
    std::vector<float> m_interp_indices;
    std::vector<float> m_interp_bufs[3];    // third buffer used as intermediate for gauss filter
    std::vector<int> m_band_widths;         // size of the band each bar represents

    // roll-off
    AVXBufR m_rolloff_modifiers;

    // gaussian filter
    Kernel<float> m_kernel;
    float m_filter_radius = 0.0f;

    // lanczos filter
    Kernel<float> m_interp_kernel;

    // slope
    AVXBufR m_slope_modifiers;

    // rounded caps
    float m_cap_radius = 0.0f;
    int m_cap_tris = 4;             // number of triangles each cap is composed of (4 min)
    std::vector<vec3> m_cap_verts;  // pre-rotated cap vertices (to be translated to final pos)

    // stepped bars
    vec3 m_step_verts[6]{};         // vertices for one step of a bar (to be translated to final pos)

    // render vars
    gs_effect_t *m_shader = nullptr;
    gs_vertbuffer_t *m_vbuf = nullptr;

    // volume normalization
    float m_input_rms = 0.0f;
    AVXBufR m_input_rms_buf;
    AVXBufR m_rms_temp_buf;     // temp buffer, bit too large for stack
    circlebuf m_rms_sync_buf{}; // A/V syncronization buffer
    size_t m_input_rms_size = 0;
    size_t m_input_rms_pos = 0;

    // FFT window
    float m_window_sum = 1.0f;

    void create_vbuf();

    void get_settings(obs_data_t *settings);

    void recapture_audio();
    void release_audio_capture();
    bool check_audio_capture(float seconds); // check if capture is valid and retry if not
    void free_bufs();

    bool sync_rms_buffer();

    void init_interp(unsigned int sz);
    void init_rolloff();
    void init_steps();

    void render_curve(gs_effect_t *effect);
    void render_bars(gs_effect_t *effect);

    gs_technique_t *get_shader_tech();
    void set_shader_vars(float cpos, float miny, float minpos, float channel_offset, float border_top, float border_bottom);

    virtual void update_input_rms() = 0;    // update RMS window

    virtual void tick_spectrum(float) = 0;  // process audio data in frequency spectrum mode
    virtual void tick_meter(float) = 0;     // process audio data in meter mode
    virtual void tick_waveform(float) = 0;  // process audio data in waveform mode

    int64_t get_audio_sync(uint64_t ts)     // get delta between end of available audio and given time in nanoseconds
    {
        auto delta = std::max(m_audio_ts, ts) - std::min(m_audio_ts, ts);
        delta = std::min(delta, MAX_TS_DELTA);
        return (m_audio_ts < ts) ? -(int64_t)delta : (int64_t)delta;
    }

    // constants
    static const float DB_MIN;
    static constexpr auto RETRY_DELAY = 2.0f;
    static constexpr uint64_t CAPTURE_TIMEOUT = 1000000ull * 500u;  // time in nanoseconds before audio capture is considered "lost" (500 ms)
    static constexpr uint64_t MAX_TS_DELTA = 1000000000ull * 16u;   // 16 seconds in ns

    inline float dbfs(float mag)
    {
        if(mag > 0.0f)
            return 20.0f * std::log10(mag);
        else
            return DB_MIN;
    }

    inline float get_gravity(float seconds)
    {
        constexpr float denom = 0.03868924705242879469662125316986f;
        constexpr float hi = denom * 5.0f;
        constexpr float lo = 0.0f;
        if((m_tsmoothing == TSmoothingMode::NONE) || (m_gravity <= 0.0f))
            return 0.0f;
        return (m_tsmoothing == TSmoothingMode::TVEXPONENTIAL) ? std::exp(-seconds / lerp(lo, hi, m_gravity)) : m_gravity;
    }

public:
    WAVSource(obs_source_t *source);
    virtual ~WAVSource();

    // no copying
    WAVSource(const WAVSource&) = delete;
    WAVSource& operator=(const WAVSource&) = delete;

    unsigned int width();
    unsigned int height();

    // main callbacks
    virtual void update(obs_data_t *settings);
    virtual void tick(float seconds);
    virtual void render(gs_effect_t *effect);

    void show();
    void hide();

    static void register_source();

    // audio capture callback
    void capture_audio(obs_source_t *source, const audio_data *audio, bool muted);

    // for capturing the final OBS audio output stream
    void capture_output_bus(size_t mix_idx, const audio_data *audio);

#ifndef DISABLE_X86_SIMD
    // constants
    static const bool HAVE_AVX2;
    static const bool HAVE_AVX;
    static const bool HAVE_FMA3;
#endif // !DISABLE_X86_SIMD
};

class WAVSourceGeneric : public WAVSource
{
protected:
    void tick_spectrum(float seconds) override;
    void tick_meter(float seconds) override;
    void tick_waveform(float seconds) override;

    void update_input_rms() override;

public:
    using WAVSource::WAVSource;
    ~WAVSourceGeneric() override = default;
};

#ifndef DISABLE_X86_SIMD

class WAVSourceAVX : public WAVSourceGeneric
{
protected:
    void tick_spectrum(float seconds) override;
    void tick_meter(float seconds) override;

    void update_input_rms() override;

public:
    using WAVSourceGeneric::WAVSourceGeneric;
    ~WAVSourceAVX() override = default;
};

class WAVSourceAVX2 : public WAVSourceAVX
{
protected:
    void tick_spectrum(float seconds) override;

public:
    using WAVSourceAVX::WAVSourceAVX;
    ~WAVSourceAVX2() override = default;
};

#endif // !DISABLE_X86_SIMD
