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

#include "waveform_config.hpp"
#include "math_funcs.hpp"
#include "source.hpp"
#include "settings.hpp"
#include <graphics/matrix4.h>
#include <vector>
#include <string>
#include <algorithm>
#include <limits>
#include "cpuinfo_x86.h"
#include <immintrin.h>

#ifndef HAVE_OBS_PROP_ALPHA
#define obs_properties_add_color_alpha obs_properties_add_color
#endif

const float WAVSource::DB_MIN = 20.0f * std::log10(std::numeric_limits<float>::min());

static const auto CPU_INFO = cpu_features::GetX86Info();
const bool WAVSource::HAVE_AVX2 = CPU_INFO.features.avx2 && CPU_INFO.features.fma3;
const bool WAVSource::HAVE_AVX = CPU_INFO.features.avx && CPU_INFO.features.fma3;
const bool WAVSource::HAVE_FMA3 = CPU_INFO.features.fma3;

static bool enum_callback(void *data, obs_source_t *src)
{
    if(obs_source_get_output_flags(src) & OBS_SOURCE_AUDIO) // filter sources without audio
        static_cast<std::vector<std::string>*>(data)->push_back(obs_source_get_name(src));
    return true;
}

static std::vector<std::string> enumerate_audio_sources()
{
    std::vector<std::string> ret;
    obs_enum_sources(&enum_callback, &ret);
    return ret;
}

static void update_audio_info(obs_audio_info *info)
{
    if(!obs_get_audio_info(info))
    {
        blog(LOG_WARNING, "[" MODULE_NAME "]: Could not determine audio configuration");
        info->samples_per_sec = 44100;
        info->speakers = SPEAKERS_UNKNOWN;
    }
}

// hide and disable a property
static inline void set_prop_visible(obs_properties_t *props, const char *prop_name, bool vis)
{
    //obs_property_set_enabled(obs_properties_get(props, prop_name), vis);
    obs_property_set_visible(obs_properties_get(props, prop_name), vis);
}

// Callbacks for obs_source_info structure
namespace callbacks {
    static const char *get_name([[maybe_unused]] void *data)
    {
        return T("source_name");
    }

    static void *create(obs_data_t *settings, obs_source_t *source)
    {
        if(WAVSource::HAVE_AVX2)
            return static_cast<void*>(new WAVSourceAVX2(settings, source));
        else if(WAVSource::HAVE_AVX)
            return static_cast<void*>(new WAVSourceAVX(settings, source));
        else
            return static_cast<void*>(new WAVSourceSSE2(settings, source));
    }

    static void destroy(void *data)
    {
        delete static_cast<WAVSource*>(data);
    }

    static uint32_t get_width(void *data)
    {
        return static_cast<WAVSource*>(data)->width();
    }

    static uint32_t get_height(void *data)
    {
        return static_cast<WAVSource*>(data)->height();
    }

    static void get_defaults(obs_data_t *settings)
    {
        obs_data_set_default_string(settings, P_AUDIO_SRC, P_NONE);
        obs_data_set_default_string(settings, P_DISPLAY_MODE, P_CURVE);
        obs_data_set_default_int(settings, P_WIDTH, 800);
        obs_data_set_default_int(settings, P_HEIGHT, 225);
        obs_data_set_default_bool(settings, P_LOG_SCALE, true);
        obs_data_set_default_bool(settings, P_RADIAL, false);
        obs_data_set_default_bool(settings, P_INVERT, false);
        obs_data_set_default_double(settings, P_DEADZONE, 20.0);
        obs_data_set_default_bool(settings, P_CAPS, false);
        obs_data_set_default_string(settings, P_CHANNEL_MODE, P_MONO);
        obs_data_set_default_int(settings, P_FFT_SIZE, 2048);
        obs_data_set_default_bool(settings, P_AUTO_FFT_SIZE, false);
        obs_data_set_default_string(settings, P_WINDOW, P_HANN);
        obs_data_set_default_string(settings, P_INTERP_MODE, P_LANCZOS);
        obs_data_set_default_string(settings, P_FILTER_MODE, P_NONE);
        obs_data_set_default_double(settings, P_FILTER_RADIUS, 1.5);
        obs_data_set_default_string(settings, P_TSMOOTHING, P_EXPAVG);
        obs_data_set_default_double(settings, P_GRAVITY, 0.65);
        obs_data_set_default_bool(settings, P_FAST_PEAKS, false);
        obs_data_set_default_int(settings, P_CUTOFF_LOW, 30);
        obs_data_set_default_int(settings, P_CUTOFF_HIGH, 17500);
        obs_data_set_default_int(settings, P_FLOOR, -65);
        obs_data_set_default_int(settings, P_CEILING, 0);
        obs_data_set_default_double(settings, P_SLOPE, 0.0);
        obs_data_set_default_string(settings, P_RENDER_MODE, P_SOLID);
        obs_data_set_default_int(settings, P_COLOR_BASE, 0xffffffff);
        obs_data_set_default_int(settings, P_COLOR_CREST, 0xffffffff);
        obs_data_set_default_double(settings, P_GRAD_RATIO, 0.75);
        obs_data_set_default_int(settings, P_BAR_WIDTH, 24);
        obs_data_set_default_int(settings, P_BAR_GAP, 6);
        obs_data_set_default_int(settings, P_STEP_WIDTH, 8);
        obs_data_set_default_int(settings, P_STEP_GAP, 4);
        obs_data_set_default_int(settings, P_METER_BUF, 150);
        obs_data_set_default_bool(settings, P_RMS_MODE, true);
    }

    static obs_properties_t *get_properties([[maybe_unused]] void *data)
    {
        auto props = obs_properties_create();

        // audio source
        auto srclist = obs_properties_add_list(props, P_AUDIO_SRC, T(P_AUDIO_SRC), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
        obs_property_list_add_string(srclist, T(P_NONE), P_NONE);
        obs_property_list_add_string(srclist, T(P_OUTPUT_BUS), P_OUTPUT_BUS);

        for(const auto& str : enumerate_audio_sources())
            obs_property_list_add_string(srclist, str.c_str(), str.c_str());

        // display type
        auto displaylist = obs_properties_add_list(props, P_DISPLAY_MODE, T(P_DISPLAY_MODE), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
        obs_property_list_add_string(displaylist, T(P_CURVE), P_CURVE);
        obs_property_list_add_string(displaylist, T(P_BARS), P_BARS);
        obs_property_list_add_string(displaylist, T(P_STEP_BARS), P_STEP_BARS);
        obs_property_list_add_string(displaylist, T(P_LEVEL_METER), P_LEVEL_METER);
        obs_property_list_add_string(displaylist, T(P_STEPPED_METER), P_STEPPED_METER);
        obs_properties_add_int(props, P_BAR_WIDTH, T(P_BAR_WIDTH), 1, 256, 1);
        obs_properties_add_int(props, P_BAR_GAP, T(P_BAR_GAP), 0, 256, 1);
        obs_properties_add_int(props, P_STEP_WIDTH, T(P_STEP_WIDTH), 1, 256, 1);
        obs_properties_add_int(props, P_STEP_GAP, T(P_STEP_GAP), 0, 256, 1);
        obs_property_set_modified_callback(displaylist, [](obs_properties_t *props, [[maybe_unused]] obs_property_t *property, obs_data_t *settings) -> bool {
            auto disp = obs_data_get_string(settings, P_DISPLAY_MODE);
            auto meter = p_equ(disp, P_LEVEL_METER);
            auto step_meter = p_equ(disp, P_STEPPED_METER);
            auto bar = p_equ(disp, P_BARS) || meter;
            auto step = p_equ(disp, P_STEP_BARS) || step_meter;
            auto curve = p_equ(disp, P_CURVE);
            set_prop_visible(props, P_BAR_WIDTH, bar || step);
            set_prop_visible(props, P_BAR_GAP, bar || step);
            set_prop_visible(props, P_STEP_WIDTH, step);
            set_prop_visible(props, P_STEP_GAP, step);
            set_prop_visible(props, P_CAPS, bar);
            obs_property_list_item_disable(obs_properties_get(props, P_RENDER_MODE), 0, !curve);

            // meter mode
            bool notmeter = !(meter || step_meter);
            set_prop_visible(props, P_SLOPE, notmeter);
            set_prop_visible(props, P_CUTOFF_LOW, notmeter);
            set_prop_visible(props, P_CUTOFF_HIGH, notmeter);
            set_prop_visible(props, P_FILTER_MODE, notmeter);
            set_prop_visible(props, P_FILTER_RADIUS, notmeter && !p_equ(obs_data_get_string(settings, P_FILTER_MODE), P_NONE));
            set_prop_visible(props, P_INTERP_MODE, notmeter);
            set_prop_visible(props, P_CHANNEL_MODE, notmeter);
            set_prop_visible(props, P_WINDOW, notmeter);
            set_prop_visible(props, P_RADIAL, notmeter);
            set_prop_visible(props, P_DEADZONE, notmeter && obs_data_get_bool(settings, P_RADIAL));
            set_prop_visible(props, P_INVERT, notmeter && obs_data_get_bool(settings, P_RADIAL));
            set_prop_visible(props, P_LOG_SCALE, notmeter);
            set_prop_visible(props, P_WIDTH, notmeter);
            set_prop_visible(props, P_AUTO_FFT_SIZE, notmeter);
            set_prop_visible(props, P_FFT_SIZE, notmeter);
            set_prop_visible(props, P_RMS_MODE, !notmeter);
            set_prop_visible(props, P_METER_BUF, !notmeter);
            return true;
            });

        // video size
        obs_properties_add_int(props, P_WIDTH, T(P_WIDTH), 32, 3840, 1);
        obs_properties_add_int(props, P_HEIGHT, T(P_HEIGHT), 32, 2160, 1);

        // log scale
        obs_properties_add_bool(props, P_LOG_SCALE, T(P_LOG_SCALE));

        // radial layout
        auto rad = obs_properties_add_bool(props, P_RADIAL, T(P_RADIAL));
        obs_properties_add_bool(props, P_INVERT, T(P_INVERT));
        auto deadzone = obs_properties_add_float_slider(props, P_DEADZONE, T(P_DEADZONE), 0.0, 100.0, 0.1);
        obs_property_float_set_suffix(deadzone, "%");
        obs_property_set_long_description(deadzone, T(P_DEADZONE_DESC));
        obs_property_set_modified_callback(rad, [](obs_properties_t *props, [[maybe_unused]] obs_property_t *property, obs_data_t *settings) -> bool {
            auto enable = obs_data_get_bool(settings, P_RADIAL) && obs_property_visible(obs_properties_get(props, P_RADIAL));
            set_prop_visible(props, P_DEADZONE, enable);
            set_prop_visible(props, P_INVERT, enable);
            return true;
            });

        // rounded caps
        auto caps = obs_properties_add_bool(props, P_CAPS, T(P_CAPS));
        obs_property_set_long_description(caps, T(P_CAPS_DESC));

        // meter
        obs_properties_add_bool(props, P_RMS_MODE, T(P_RMS_MODE));
        auto meterbuf = obs_properties_add_int_slider(props, P_METER_BUF, T(P_METER_BUF), 16, 1000, 1);
        obs_property_int_set_suffix(meterbuf, " ms");

        // channels
        auto chanlst = obs_properties_add_list(props, P_CHANNEL_MODE, T(P_CHANNEL_MODE), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
        obs_property_list_add_string(chanlst, T(P_MONO), P_MONO);
        obs_property_list_add_string(chanlst, T(P_STEREO), P_STEREO);
        obs_property_set_long_description(chanlst, T(P_CHAN_DESC));

        // fft size
        auto autofftsz = obs_properties_add_bool(props, P_AUTO_FFT_SIZE, T(P_AUTO_FFT_SIZE));
        auto fftsz = obs_properties_add_int_slider(props, P_FFT_SIZE, T(P_FFT_SIZE), 128, 4096, 64);
        obs_property_set_long_description(autofftsz, T(P_AUTO_FFT_DESC));
        obs_property_set_long_description(fftsz, T(P_FFT_DESC));
        obs_property_set_modified_callback(autofftsz, [](obs_properties_t *props, [[maybe_unused]] obs_property_t *property, obs_data_t *settings) -> bool {
            auto enable = !obs_data_get_bool(settings, P_AUTO_FFT_SIZE);
            obs_property_set_enabled(obs_properties_get(props, P_FFT_SIZE), enable);
            return true;
            });

        // fft window function
        auto wndlist = obs_properties_add_list(props, P_WINDOW, T(P_WINDOW), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
        obs_property_list_add_string(wndlist, T(P_NONE), P_NONE);
        obs_property_list_add_string(wndlist, T(P_HANN), P_HANN);
        obs_property_list_add_string(wndlist, T(P_HAMMING), P_HAMMING);
        obs_property_list_add_string(wndlist, T(P_BLACKMAN), P_BLACKMAN);
        obs_property_list_add_string(wndlist, T(P_BLACKMAN_HARRIS), P_BLACKMAN_HARRIS);
        obs_property_set_long_description(wndlist, T(P_WINDOW_DESC));

        // smoothing
        auto tsmoothlist = obs_properties_add_list(props, P_TSMOOTHING, T(P_TSMOOTHING), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
        obs_property_list_add_string(tsmoothlist, T(P_NONE), P_NONE);
        obs_property_list_add_string(tsmoothlist, T(P_EXPAVG), P_EXPAVG);
        auto grav = obs_properties_add_float_slider(props, P_GRAVITY, T(P_GRAVITY), 0.0, 1.0, 0.01);
        auto peaks = obs_properties_add_bool(props, P_FAST_PEAKS, T(P_FAST_PEAKS));
        obs_property_set_long_description(tsmoothlist, T(P_TEMPORAL_DESC));
        obs_property_set_long_description(grav, T(P_GRAVITY_DESC));
        obs_property_set_long_description(peaks, T(P_FAST_PEAKS_DESC));
        obs_property_set_modified_callback(tsmoothlist, [](obs_properties_t *props, [[maybe_unused]] obs_property_t *property, obs_data_t *settings) -> bool {
            auto enable = !p_equ(obs_data_get_string(settings, P_TSMOOTHING), P_NONE);
            set_prop_visible(props, P_GRAVITY, enable);
            set_prop_visible(props, P_FAST_PEAKS, enable);
            return true;
            });

        // interpolation
        auto interplist = obs_properties_add_list(props, P_INTERP_MODE, T(P_INTERP_MODE), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
        obs_property_list_add_string(interplist, T(P_POINT), P_POINT);
        obs_property_list_add_string(interplist, T(P_LANCZOS), P_LANCZOS);
        obs_property_set_long_description(interplist, T(P_INTERP_DESC));

        // filter
        auto filterlist = obs_properties_add_list(props, P_FILTER_MODE, T(P_FILTER_MODE), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
        obs_property_list_add_string(filterlist, T(P_NONE), P_NONE);
        obs_property_list_add_string(filterlist, T(P_GAUSS), P_GAUSS);
        obs_properties_add_float_slider(props, P_FILTER_RADIUS, T(P_FILTER_RADIUS), 0.0, 32.0, 0.01);
        obs_property_set_long_description(filterlist, T(P_FILTER_DESC));
        obs_property_set_modified_callback(filterlist, [](obs_properties_t *props, [[maybe_unused]] obs_property_t *property, obs_data_t *settings) -> bool {
            auto enable = !p_equ(obs_data_get_string(settings, P_FILTER_MODE), P_NONE) && obs_property_visible(obs_properties_get(props, P_FILTER_MODE));
            set_prop_visible(props, P_FILTER_RADIUS, enable);
            return true;
            });

        // display
        auto low_cut = obs_properties_add_int_slider(props, P_CUTOFF_LOW, T(P_CUTOFF_LOW), 0, 24000, 1);
        auto high_cut = obs_properties_add_int_slider(props, P_CUTOFF_HIGH, T(P_CUTOFF_HIGH), 0, 24000, 1);
        obs_property_int_set_suffix(low_cut, " Hz");
        obs_property_int_set_suffix(high_cut, " Hz");
        auto floor = obs_properties_add_int_slider(props, P_FLOOR, T(P_FLOOR), -120, 0, 1);
        auto ceiling = obs_properties_add_int_slider(props, P_CEILING, T(P_CEILING), -120, 0, 1);
        obs_property_int_set_suffix(floor, " dBFS");
        obs_property_int_set_suffix(ceiling, " dBFS");
        auto slope = obs_properties_add_float_slider(props, P_SLOPE, T(P_SLOPE), 0.0, 10.0, 0.01);
        obs_property_set_long_description(slope, T(P_SLOPE_DESC));
        auto renderlist = obs_properties_add_list(props, P_RENDER_MODE, T(P_RENDER_MODE), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
        obs_property_list_add_string(renderlist, T(P_LINE), P_LINE);
        obs_property_list_add_string(renderlist, T(P_SOLID), P_SOLID);
        obs_property_list_add_string(renderlist, T(P_GRADIENT), P_GRADIENT);
        obs_properties_add_color_alpha(props, P_COLOR_BASE, T(P_COLOR_BASE));
        obs_properties_add_color_alpha(props, P_COLOR_CREST, T(P_COLOR_CREST));
        obs_properties_add_float_slider(props, P_GRAD_RATIO, T(P_GRAD_RATIO), 0.0, 4.0, 0.01);
        obs_property_set_modified_callback(renderlist, [](obs_properties_t *props, [[maybe_unused]] obs_property_t *property, obs_data_t *settings) -> bool {
            auto enable = p_equ(obs_data_get_string(settings, P_RENDER_MODE), P_GRADIENT);
            obs_property_set_enabled(obs_properties_get(props, P_COLOR_CREST), enable);
            set_prop_visible(props, P_GRAD_RATIO, enable);
            return true;
            });

        return props;
    }

    static void update(void *data, obs_data_t *settings)
    {
        static_cast<WAVSource*>(data)->update(settings);
    }

    static void show(void *data)
    {
        static_cast<WAVSource*>(data)->show();
    }

    static void hide(void *data)
    {
        static_cast<WAVSource*>(data)->hide();
    }

    static void tick(void *data, float seconds)
    {
        static_cast<WAVSource*>(data)->tick(seconds);
    }

    static void render(void *data, gs_effect_t *effect)
    {
        static_cast<WAVSource*>(data)->render(effect);
    }

    static void capture_audio(void *data, obs_source_t *source, const audio_data *audio, bool muted)
    {
        static_cast<WAVSource*>(data)->capture_audio(source, audio, muted);
    }

    static void capture_output_bus(void *param, size_t mix_idx, audio_data *data)
    {
        static_cast<WAVSource*>(param)->capture_output_bus(mix_idx, data);
    }
}

void WAVSource::get_settings(obs_data_t *settings)
{
    auto src_name = obs_data_get_string(settings, P_AUDIO_SRC);
    m_width = (unsigned int)obs_data_get_int(settings, P_WIDTH);
    m_height = (unsigned int)obs_data_get_int(settings, P_HEIGHT);
    m_log_scale = obs_data_get_bool(settings, P_LOG_SCALE);
    m_radial = obs_data_get_bool(settings, P_RADIAL);
    m_invert = obs_data_get_bool(settings, P_INVERT);
    auto deadzone = (float)obs_data_get_double(settings, P_DEADZONE) / 100.0f;
    m_rounded_caps = obs_data_get_bool(settings, P_CAPS);
    m_stereo = p_equ(obs_data_get_string(settings, P_CHANNEL_MODE), P_STEREO);
    m_fft_size = (size_t)obs_data_get_int(settings, P_FFT_SIZE);
    m_auto_fft_size = obs_data_get_bool(settings, P_AUTO_FFT_SIZE);
    auto wnd = obs_data_get_string(settings, P_WINDOW);
    auto tsmoothing = obs_data_get_string(settings, P_TSMOOTHING);
    m_gravity = (float)obs_data_get_double(settings, P_GRAVITY);
    m_fast_peaks = obs_data_get_bool(settings, P_FAST_PEAKS);
    auto interp = obs_data_get_string(settings, P_INTERP_MODE);
    auto filtermode = obs_data_get_string(settings, P_FILTER_MODE);
    m_filter_radius = (float)obs_data_get_double(settings, P_FILTER_RADIUS);
    m_cutoff_low = (int)obs_data_get_int(settings, P_CUTOFF_LOW);
    m_cutoff_high = (int)obs_data_get_int(settings, P_CUTOFF_HIGH);
    m_floor = (int)obs_data_get_int(settings, P_FLOOR);
    m_ceiling = (int)obs_data_get_int(settings, P_CEILING);
    m_slope = (float)obs_data_get_double(settings, P_SLOPE);
    auto rendermode = obs_data_get_string(settings, P_RENDER_MODE);
    auto color_base = obs_data_get_int(settings, P_COLOR_BASE);
    auto color_crest = obs_data_get_int(settings, P_COLOR_CREST);
    m_grad_ratio = (float)obs_data_get_double(settings, P_GRAD_RATIO);
    auto display = obs_data_get_string(settings, P_DISPLAY_MODE);
    m_bar_width = (int)obs_data_get_int(settings, P_BAR_WIDTH);
    m_bar_gap = (int)obs_data_get_int(settings, P_BAR_GAP);
    m_step_width = (int)obs_data_get_int(settings, P_STEP_WIDTH);
    m_step_gap = (int)obs_data_get_int(settings, P_STEP_GAP);
    m_meter_rms = obs_data_get_bool(settings, P_RMS_MODE);
    m_meter_ms = (int)obs_data_get_int(settings, P_METER_BUF);

    m_color_base = { (uint8_t)color_base / 255.0f, (uint8_t)(color_base >> 8) / 255.0f, (uint8_t)(color_base >> 16) / 255.0f, (uint8_t)(color_base >> 24) / 255.0f };
    m_color_crest = { (uint8_t)color_crest / 255.0f, (uint8_t)(color_crest >> 8) / 255.0f, (uint8_t)(color_crest >> 16) / 255.0f, (uint8_t)(color_crest >> 24) / 255.0f };

    if(m_fft_size < 128)
        m_fft_size = 128;
    else if(m_fft_size & 15)
        m_fft_size &= -16; // align to 64-byte multiple so that N/2 is AVX aligned

    if((m_cutoff_high - m_cutoff_low) < 1)
    {
        m_cutoff_high = 17500;
        m_cutoff_low = 120;
    }

    if((m_ceiling - m_floor) < 1)
    {
        m_ceiling = 0;
        m_floor = -120;
    }

    if(src_name != nullptr)
        m_audio_source_name = src_name;
    else
        m_audio_source_name.clear();

    if(p_equ(wnd, P_HANN))
        m_window_func = FFTWindow::HANN;
    else if(p_equ(wnd, P_HAMMING))
        m_window_func = FFTWindow::HAMMING;
    else if(p_equ(wnd, P_BLACKMAN))
        m_window_func = FFTWindow::BLACKMAN;
    else if(p_equ(wnd, P_BLACKMAN_HARRIS))
        m_window_func = FFTWindow::BLACKMAN_HARRIS;
    else
        m_window_func = FFTWindow::NONE;

    if(p_equ(interp, P_LANCZOS))
        m_interp_mode = InterpMode::LANCZOS;
    else
        m_interp_mode = InterpMode::POINT;

    if(p_equ(filtermode, P_GAUSS))
        m_filter_mode = FilterMode::GAUSS;
    else
        m_filter_mode = FilterMode::NONE;

    if(p_equ(tsmoothing, P_EXPAVG))
        m_tsmoothing = TSmoothingMode::EXPONENTIAL;
    else
        m_tsmoothing = TSmoothingMode::NONE;

    if(p_equ(rendermode, P_LINE))
        m_render_mode = RenderMode::LINE;
    else if(p_equ(rendermode, P_SOLID))
        m_render_mode = RenderMode::SOLID;
    else
        m_render_mode = RenderMode::GRADIENT;

    if(p_equ(display, P_BARS))
        m_display_mode = DisplayMode::BAR;
    else if(p_equ(display, P_STEP_BARS))
        m_display_mode = DisplayMode::STEPPED_BAR;
    else if(p_equ(display, P_LEVEL_METER))
        m_display_mode = DisplayMode::METER;
    else if(p_equ(display, P_STEPPED_METER))
        m_display_mode = DisplayMode::STEPPED_METER;
    else
        m_display_mode = DisplayMode::CURVE;

    if((m_display_mode != DisplayMode::BAR) && (m_display_mode != DisplayMode::METER))
        m_rounded_caps = false;

    m_meter_mode = false;
    if((m_display_mode == DisplayMode::METER) || (m_display_mode == DisplayMode::STEPPED_METER))
    {
        m_radial = false;
        m_meter_mode = true;
    }

    if(m_radial)
    {
        m_height /= 2; // fit diameter to hieght of bounding box
        auto max_deadzone = (float)(m_height - 16);
        if(m_rounded_caps)
            max_deadzone = std::max(max_deadzone - m_bar_width, 0.0f);
        m_deadzone = std::min(std::floor((float)m_height * deadzone), max_deadzone);
        m_height -= (int)m_deadzone;
    }
}

void WAVSource::recapture_audio()
{
    // release old capture
    release_audio_capture();

    // add new capture
    auto src_name = m_audio_source_name.c_str();
    if(p_equ(src_name, P_OUTPUT_BUS))
    {
        audio_convert_info cvt{};
        cvt.format = audio_format::AUDIO_FORMAT_FLOAT_PLANAR;
        cvt.samples_per_sec = m_audio_info.samples_per_sec;
        cvt.speakers = (m_audio_info.speakers != speaker_layout::SPEAKERS_UNKNOWN) ? m_audio_info.speakers : speaker_layout::SPEAKERS_STEREO;
        m_output_bus_captured = audio_output_connect(obs_get_audio(), 0, &cvt, &callbacks::capture_output_bus, this);
    }
    else
    {
        auto asrc = obs_get_source_by_name(src_name);
        if(asrc != nullptr)
        {
            obs_source_add_audio_capture_callback(asrc, &callbacks::capture_audio, this);
            m_audio_source = obs_source_get_weak_source(asrc);
            obs_source_release(asrc);
        }
        else if(!p_equ(src_name, "none"))
        {
            if(m_retries++ == 0)
                blog(LOG_WARNING, "[" MODULE_NAME "]: Failed to get audio source: \"%s\"", src_name);
        }
    }
}

void WAVSource::release_audio_capture()
{
    if(m_audio_source != nullptr)
    {
        auto src = obs_weak_source_get_source(m_audio_source);
        obs_weak_source_release(m_audio_source);
        m_audio_source = nullptr;
        if(src != nullptr)
        {
            obs_source_remove_audio_capture_callback(src, &callbacks::capture_audio, this);
            obs_source_release(src);
        }
    }

    if(m_output_bus_captured)
    {
        audio_output_disconnect(obs_get_audio(), 0, &callbacks::capture_output_bus, this);
        m_output_bus_captured = false;
    }

    // reset circular buffers
    for(auto& i : m_capturebufs)
    {
        i.end_pos = 0;
        i.start_pos = 0;
        i.size = 0;
    }
}

bool WAVSource::check_audio_capture(float seconds)
{
    if(m_output_bus_captured)
        return true;

    if(m_audio_source == nullptr)
    {
        m_next_retry -= seconds;
        if(m_next_retry <= 0.0f)
        {
            m_next_retry = RETRY_DELAY;
            recapture_audio();
            if(m_audio_source != nullptr)
                return true;
        }
        return false;
    }

    // check if the source still exists
    auto src = obs_weak_source_get_source(m_audio_source);
    if(src == nullptr)
    {
        release_audio_capture();
        return false;
    }
    obs_source_release(src);
    return true;
}

void WAVSource::free_bufs()
{
    for(auto i = 0; i < 2; ++i)
    {
        m_decibels[i].reset();
        m_tsmooth_buf[i].reset();
    }

    m_fft_input.reset();
    m_fft_output.reset();
    m_window_coefficients.reset();
    m_slope_modifiers.reset();

    if(m_fft_plan != nullptr)
    {
        fftwf_destroy_plan(m_fft_plan);
        m_fft_plan = nullptr;
    }

    m_fft_size = 0;
}

void WAVSource::init_interp(unsigned int sz)
{
    const auto maxbin = (m_fft_size / 2) - 1;
    const auto sr = (float)m_audio_info.samples_per_sec;
    const auto lowbin = std::clamp((float)m_cutoff_low * m_fft_size / sr, 1.0f, (float)maxbin);
    const auto highbin = std::clamp((float)m_cutoff_high * m_fft_size / sr, 1.0f, (float)maxbin);

    m_interp_indices.resize(sz);
    if(m_log_scale)
    {
        for(auto i = 0u; i < sz; ++i)
            m_interp_indices[i] = log_interp(lowbin, highbin, (float)i / (float)(sz - 1));
    }
    else
    {
        for(auto i = 0u; i < sz; ++i)
            m_interp_indices[i] = lerp(lowbin, highbin, (float)i / (float)(sz - 1));
    }
}

WAVSource::WAVSource(obs_data_t *settings, obs_source_t *source)
{
    m_source = source;
    for(auto& i : m_capturebufs)
        circlebuf_init(&i);
    update(settings);
}

WAVSource::~WAVSource()
{
    std::lock_guard lock(m_mtx);
    release_audio_capture();
    free_bufs();

    for(auto& i : m_capturebufs)
        circlebuf_free(&i);
}

unsigned int WAVSource::width()
{
    std::lock_guard lock(m_mtx);

    if(m_meter_mode)
        return (m_bar_width * m_capture_channels) + ((m_capture_channels > 1) ? m_bar_gap : 0);
    if(m_radial)
        return (unsigned int)((m_height + m_deadzone) * 2);
    return m_width;
}

unsigned int WAVSource::height()
{
    std::lock_guard lock(m_mtx);

    if(m_radial)
        return (unsigned int)((m_height + m_deadzone) * 2);
    return m_height;
}

void WAVSource::update(obs_data_t *settings)
{
    std::lock_guard lock(m_mtx);

    release_audio_capture();
    free_bufs();
    get_settings(settings);

    // get current audio settings
    update_audio_info(&m_audio_info);
    m_capture_channels = std::min(get_audio_channels(m_audio_info.speakers), 2u);
    if(m_capture_channels == 0)
        blog(LOG_WARNING, "[" MODULE_NAME "]: Could not determine audio channel count");

    // meter mode
    if(m_meter_mode)
    {
        // turn off stuff we don't need in this mode
        m_window_func = FFTWindow::NONE;
        m_interp_mode = InterpMode::POINT;
        m_filter_mode = FilterMode::NONE;
        m_auto_fft_size = false;
        m_slope = 0.0f;
        m_stereo = false;
        m_radial = false;

        // repurpose m_fft_size for meter buffer size
        m_fft_size = size_t(m_audio_info.samples_per_sec * (m_meter_ms / 1000.0)) & -16;

        memset(m_meter_pos, 0, sizeof(m_meter_pos));
        for(auto& i : m_meter_buf)
            i = DB_MIN;
        for(auto& i : m_meter_val)
            i = DB_MIN;
    }

    // calculate FFT size based on video FPS
    obs_video_info vinfo = {};
    if(obs_get_video_info(&vinfo))
        m_fps = double(vinfo.fps_num) / double(vinfo.fps_den);
    else
        m_fps = 60.0;
    if(m_auto_fft_size)
    {
        // align to 64-byte multiple so that N/2 is AVX aligned
        m_fft_size = size_t(m_audio_info.samples_per_sec / m_fps) & -16;
        if(m_fft_size < 128)
            m_fft_size = 128;
    }

    // alloc fftw buffers
    m_output_channels = ((m_capture_channels > 1) || m_stereo) ? 2u : 1u;
    for(auto i = 0u; i < m_output_channels; ++i)
    {
        auto count = m_meter_mode ? m_fft_size : m_fft_size / 2;
        m_decibels[i].reset(avx_alloc<float>(count));
        if(m_meter_mode)
            memset(m_decibels[i].get(), 0, count * sizeof(float));
        else
        {
            if(m_tsmoothing != TSmoothingMode::NONE)
                m_tsmooth_buf[i].reset(avx_alloc<float>(count));
            for(auto j = 0u; j < count; ++j)
            {
                m_decibels[i][j] = DB_MIN;
                if(m_tsmoothing != TSmoothingMode::NONE)
                    m_tsmooth_buf[i][j] = 0;
            }
        }
    }
    if(!m_meter_mode)
    {
        m_fft_input.reset(avx_alloc<float>(m_fft_size));
        m_fft_output.reset(avx_alloc<fftwf_complex>(m_fft_size));
        m_fft_plan = fftwf_plan_dft_r2c_1d((int)m_fft_size, m_fft_input.get(), m_fft_output.get(), FFTW_ESTIMATE);
    }

    // window function
    if(m_window_func != FFTWindow::NONE)
    {
        // precompute window coefficients
        m_window_coefficients.reset(avx_alloc<float>(m_fft_size));
        const auto N = m_fft_size - 1;
        constexpr auto pi2 = 2 * (float)M_PI;
        constexpr auto pi4 = 4 * (float)M_PI;
        constexpr auto pi6 = 6 * (float)M_PI;
        switch(m_window_func)
        {
        case FFTWindow::HAMMING:
            for(size_t i = 0; i < m_fft_size; ++i)
                m_window_coefficients[i] = 0.53836f - (0.46164f * std::cos((pi2 * i) / N));
            break;

        case FFTWindow::BLACKMAN:
            for(size_t i = 0; i < m_fft_size; ++i)
                m_window_coefficients[i] = 0.42f - (0.5f * std::cos((pi2 * i) / N)) + (0.08f * std::cos((pi4 * i) / N));
            break;

        case FFTWindow::BLACKMAN_HARRIS:
            for(size_t i = 0; i < m_fft_size; ++i)
                m_window_coefficients[i] = 0.35875f - (0.48829f * std::cos((pi2 * i) / N)) + (0.14128f * std::cos((pi4 * i) / N)) - (0.01168f * std::cos((pi6 * i) / N));
            break;

        case FFTWindow::HANN:
        default:
            for(size_t i = 0; i < m_fft_size; ++i)
                m_window_coefficients[i] = 0.5f * (1 - std::cos((pi2 * i) / N));
            break;
        }
    }

    m_last_silent = false;
    m_show = true;
    m_retries = 0;
    m_next_retry = 0.0f;

    recapture_audio();
    for(auto& i : m_capturebufs)
    {
        auto bufsz = m_fft_size * sizeof(float);
        if(i.size < bufsz)
            circlebuf_push_back_zero(&i, bufsz - i.size);
    }

    // precomupte interpolated indices
    if(m_display_mode == DisplayMode::CURVE)
    {
        init_interp(m_width);
        for(auto& i : m_interp_bufs)
            i.resize(m_width);
    }
    else if(m_meter_mode)
    {
        // channel meter rendering through the bar renderer
        // emulate 1-2 bar spectrum graph
        m_interp_indices.clear();
        for(auto& i : m_interp_bufs)
            i.clear();
        m_interp_bufs[0].resize(m_capture_channels);
        m_num_bars = m_capture_channels;
    }
    else
    {
        const auto bar_stride = m_bar_width + m_bar_gap;
        m_num_bars = (int)(m_width / bar_stride);
        if(((int)m_width - (m_num_bars * bar_stride)) >= m_bar_width)
            ++m_num_bars;
        init_interp(m_num_bars + 1); // make extra band for last bar
        for(auto& i : m_interp_bufs)
            i.resize(m_num_bars);
    }

    // filter
    if(m_filter_mode == FilterMode::GAUSS)
        m_kernel = make_gauss_kernel(m_filter_radius);

    // slope
    const auto num_mods = m_fft_size / 2;
    const auto maxmod = (float)(num_mods - 1);
    m_slope_modifiers.reset(avx_alloc<float>(num_mods));
    for(size_t i = 0; i < num_mods; ++i)
        m_slope_modifiers[i] = log10(log_interp(10.0f, 10000.0f, ((float)i * m_slope) / maxmod));

    // rounded caps
    m_cap_verts.clear();
    if(m_rounded_caps)
    {
        // caps are full circles to avoid distortion issues in radial mode
        m_cap_radius = (float)m_bar_width / 2.0f;
        m_cap_tris = std::max((int)((2 * (float)M_PI * m_cap_radius) / 3.0f), 4);
        auto angle = (2 * (float)M_PI) / (float)m_cap_tris;
        auto verts = m_cap_tris + 1;
        m_cap_verts.resize(verts);
        for(auto j = 0; j < verts; ++j)
        {
            auto a = j * angle;
            m_cap_verts[j].x = m_cap_radius * std::cos(a);
            m_cap_verts[j].y = m_cap_radius * std::sin(a);
        }
    }
}

void WAVSource::tick(float seconds)
{
    std::lock_guard lock(m_mtx);
    if(m_meter_mode)
        tick_meter(seconds);
    else
        tick_spectrum(seconds);
}

void WAVSource::render([[maybe_unused]] gs_effect_t *effect)
{
    std::lock_guard lock(m_mtx);
    if(m_display_mode == DisplayMode::CURVE)
        render_curve(effect);
    else
        render_bars(effect);
}

void WAVSource::render_curve([[maybe_unused]] gs_effect_t *effect)
{
    //std::lock_guard lock(m_mtx); // now locked in render()
    //if(m_last_silent)
    //    return;

    // vertex buffer
    const auto num_verts = (size_t)((m_render_mode == RenderMode::LINE) ? m_width : (m_width + 2));
    auto vbdata = gs_vbdata_create();
    vbdata->num = num_verts;
    vbdata->points = (vec3*)bmalloc(num_verts * sizeof(vec3));
    vbdata->num_tex = 1;
    vbdata->tvarray = (gs_tvertarray*)bzalloc(sizeof(gs_tvertarray));
    vbdata->tvarray->width = 2;
    vbdata->tvarray->array = bmalloc(2 * num_verts * sizeof(float));
    auto vbuf = gs_vertexbuffer_create(vbdata, GS_DYNAMIC);

    auto filename = obs_module_file("gradient.effect");
    auto shader = gs_effect_create_from_file(filename, nullptr);
    bfree(filename);

    const char *techname;
    if(m_radial)
        techname = (m_render_mode == RenderMode::GRADIENT) ? "RadialGradient" : "Radial";
    else
        techname = (m_render_mode == RenderMode::GRADIENT) ? "Gradient" : "Solid";
    auto tech = gs_effect_get_technique(shader, techname);
    
    const auto center = (float)m_height / 2 + 0.5f;
    const auto right = (float)m_width + 0.5f;
    const auto bottom = (float)m_height + 0.5f;
    const auto dbrange = m_ceiling - m_floor;
    const auto cpos = m_stereo ? center : bottom;

    auto grad_center = gs_effect_get_param_by_name(shader, "grad_center");
    gs_effect_set_float(grad_center, cpos);
    auto color_base = gs_effect_get_param_by_name(shader, "color_base");
    gs_effect_set_vec4(color_base, &m_color_base);
    auto color_crest = gs_effect_get_param_by_name(shader, "color_crest");
    gs_effect_set_vec4(color_crest, &m_color_crest);

    if(m_radial)
    {
        auto graph_width = gs_effect_get_param_by_name(shader, "graph_width");
        gs_effect_set_float(graph_width, (float)m_width);
        auto graph_height = gs_effect_get_param_by_name(shader, "graph_height");
        gs_effect_set_float(graph_height, (float)m_height);
        auto graph_deadzone = gs_effect_get_param_by_name(shader, "graph_deadzone");
        gs_effect_set_float(graph_deadzone, m_deadzone);
        auto graph_invert = gs_effect_get_param_by_name(shader, "graph_invert");
        gs_effect_set_bool(graph_invert, m_invert);
        auto radial_center = gs_effect_get_param_by_name(shader, "radial_center");
        vec2 rc;
        vec2_set(&rc, (float)m_height + m_deadzone, (float)m_height + m_deadzone);
        gs_effect_set_vec2(radial_center, &rc);
    }

    // interpolation
    auto miny = cpos;
    for(auto channel = 0u; channel < (m_stereo ? 2u : 1u); ++channel)
    {
        if(m_interp_mode == InterpMode::LANCZOS)
            for(auto i = 0u; i < m_width; ++i)
                m_interp_bufs[channel][i] = lanczos_interp(m_interp_indices[i], 3.0f, m_fft_size / 2, m_decibels[channel].get());
        else
            for(auto i = 0u; i < m_width; ++i)
                m_interp_bufs[channel][i] = m_decibels[channel][(int)m_interp_indices[i]];

        if(m_filter_mode != FilterMode::NONE)
        {
            if(HAVE_AVX)
                m_interp_bufs[channel] = apply_filter_fma3(m_interp_bufs[channel], m_kernel);
            else
                m_interp_bufs[channel] = apply_filter(m_interp_bufs[channel], m_kernel);
        }
        
        const auto step = (m_render_mode == RenderMode::LINE) ? 1 : 2;
        for(auto i = 0u; i < m_width; i += step)
        {
            auto val = lerp(0.5f, cpos, std::clamp(m_ceiling - m_interp_bufs[channel][i], 0.0f, (float)dbrange) / dbrange);
            if(val < miny)
                miny = val;
            m_interp_bufs[channel][i] = val;
        }
    }
    auto grad_height = gs_effect_get_param_by_name(shader, "grad_height");
    gs_effect_set_float(grad_height, (cpos - miny) * m_grad_ratio);

    gs_technique_begin(tech);
    gs_technique_begin_pass(tech, 0);
    gs_load_vertexbuffer(vbuf);
    gs_load_indexbuffer(nullptr);

    for(auto channel = 0u; channel < (m_stereo ? 2u : 1u); ++channel)
    {
        auto vertpos = 0u;
        vbdata = gs_vertexbuffer_get_data(vbuf);
        if(m_render_mode != RenderMode::LINE)
            vec3_set(&vbdata->points[vertpos++], -0.5, cpos, 0);

        for(auto i = 0u; i < m_width; ++i)
        {
            if((m_render_mode != RenderMode::LINE) && (i & 1))
            {
                vec3_set(&vbdata->points[vertpos++], (float)i + 0.5f, cpos, 0);
                continue;
            }

            auto val = m_interp_bufs[channel][i];
            if(channel == 0)
                vec3_set(&vbdata->points[vertpos++], (float)i + 0.5f, val, 0);
            else
                vec3_set(&vbdata->points[vertpos++], (float)i + 0.5f, bottom - val, 0);
        }

        if(m_render_mode != RenderMode::LINE)
            vec3_set(&vbdata->points[vertpos++], right, cpos, 0);

        gs_vertexbuffer_flush(vbuf);

        gs_draw((m_render_mode != RenderMode::LINE) ? GS_TRISTRIP : GS_LINESTRIP, 0, (uint32_t)num_verts);
    }

    gs_load_vertexbuffer(nullptr);
    gs_vertexbuffer_destroy(vbuf);
    gs_technique_end_pass(tech);
    gs_technique_end(tech);

    gs_effect_destroy(shader);
}

// FIXME: DESPERATELY needs cleanup
void WAVSource::render_bars([[maybe_unused]] gs_effect_t *effect)
{
    //std::lock_guard lock(m_mtx); // now locked in render()
    //if(m_last_silent)
    //    return;

    auto filename = obs_module_file("gradient.effect");
    auto shader = gs_effect_create_from_file(filename, nullptr);
    bfree(filename);

    const char *techname;
    if(m_radial)
        techname = (m_render_mode == RenderMode::GRADIENT) ? "RadialGradient" : "Radial";
    else
        techname = (m_render_mode == RenderMode::GRADIENT) ? "Gradient" : "Solid";
    auto tech = gs_effect_get_technique(shader, techname);

    const auto bar_stride = m_bar_width + m_bar_gap;
    const auto step_stride = m_step_width + m_step_gap;
    const auto center = (float)m_height / 2 + 0.5f;
    const auto bottom = (float)m_height + 0.5f;
    const auto dbrange = m_ceiling - m_floor;
    const auto cpos = m_stereo ? center : bottom;

    auto max_steps = (size_t)(cpos / step_stride);
    if(((int)cpos - (int)(max_steps * step_stride)) >= m_step_width)
        ++max_steps;

    // vertex buffer
    auto num_verts = (size_t)(m_num_bars * 6);
    if((m_display_mode == DisplayMode::STEPPED_BAR) || (m_display_mode == DisplayMode::STEPPED_METER))
        num_verts *= max_steps;
    else if(m_rounded_caps)
        num_verts += m_cap_tris * 6 * m_num_bars; // 2 caps per bar
    auto vbdata = gs_vbdata_create();
    vbdata->num = num_verts;
    vbdata->points = (vec3*)bmalloc(num_verts * sizeof(vec3));
    vbdata->num_tex = 1;
    vbdata->tvarray = (gs_tvertarray*)bzalloc(sizeof(gs_tvertarray));
    vbdata->tvarray->width = 2;
    vbdata->tvarray->array = bmalloc(2 * num_verts * sizeof(float));
    auto vbuf = gs_vertexbuffer_create(vbdata, GS_DYNAMIC);

    auto grad_center = gs_effect_get_param_by_name(shader, "grad_center");
    gs_effect_set_float(grad_center, cpos);
    auto color_base = gs_effect_get_param_by_name(shader, "color_base");
    gs_effect_set_vec4(color_base, &m_color_base);
    auto color_crest = gs_effect_get_param_by_name(shader, "color_crest");
    gs_effect_set_vec4(color_crest, &m_color_crest);

    if(m_radial)
    {
        auto graph_width = gs_effect_get_param_by_name(shader, "graph_width");
        gs_effect_set_float(graph_width, (float)m_width);
        auto graph_height = gs_effect_get_param_by_name(shader, "graph_height");
        gs_effect_set_float(graph_height, (float)m_height);
        auto graph_deadzone = gs_effect_get_param_by_name(shader, "graph_deadzone");
        gs_effect_set_float(graph_deadzone, m_deadzone);
        auto graph_invert = gs_effect_get_param_by_name(shader, "graph_invert");
        gs_effect_set_bool(graph_invert, m_invert);
        auto radial_center = gs_effect_get_param_by_name(shader, "radial_center");
        vec2 rc;
        vec2_set(&rc, (float)m_height + m_deadzone, (float)m_height + m_deadzone);
        gs_effect_set_vec2(radial_center, &rc);
    }

    // interpolation
    auto miny = cpos;
    for(auto channel = 0u; channel < (m_stereo ? 2u : 1u); ++channel)
    {
        if(m_meter_mode)
        {
            for(auto i = 0u; i < m_capture_channels; ++i)
                m_interp_bufs[0][i] = m_meter_val[i];
        }
        else
        {
            if(m_interp_mode == InterpMode::LANCZOS)
            {
                for(auto i = 0; i < m_num_bars; ++i)
                {
                    auto pos = m_interp_indices[i];
                    float sum = 0.0f;
                    int count = 0;
                    float stop = m_interp_indices[i + 1];
                    do
                    {
                        sum += lanczos_interp(pos, 3.0f, m_fft_size / 2, m_decibels[channel].get());
                        ++count;
                        pos += 1.0f;
                    } while(pos < stop);
                    m_interp_bufs[channel][i] = sum / (float)count;
                }
            }
            else
            {
                for(auto i = 0; i < m_num_bars; ++i)
                {
                    auto pos = (int)m_interp_indices[i];
                    float sum = 0.0f;
                    int count = 0;
                    int stop = (int)m_interp_indices[i + 1];
                    do
                    {
                        sum += m_decibels[channel][pos];
                        ++count;
                        ++pos;
                    } while(pos < stop);
                    m_interp_bufs[channel][i] = sum / (float)count;
                }
            }

            if(m_filter_mode != FilterMode::NONE)
            {
                if(HAVE_AVX)
                    m_interp_bufs[channel] = apply_filter_fma3(m_interp_bufs[channel], m_kernel);
                else
                    m_interp_bufs[channel] = apply_filter(m_interp_bufs[channel], m_kernel);
            }
        }

        auto border_top = (m_rounded_caps) ? m_cap_radius : 0.5f;
        auto border_bottom = (m_rounded_caps && !m_stereo) ? cpos - m_cap_radius : cpos;
        for(auto i = 0; i < m_num_bars; ++i)
        {
            auto val = lerp(border_top, border_bottom, std::clamp(m_ceiling - m_interp_bufs[channel][i], 0.0f, (float)dbrange) / dbrange);
            if(val < miny)
                miny = val;
            m_interp_bufs[channel][i] = val;
        }
    }
    auto grad_height = gs_effect_get_param_by_name(shader, "grad_height");
    gs_effect_set_float(grad_height, (cpos - miny) * m_grad_ratio);

    gs_technique_begin(tech);
    gs_technique_begin_pass(tech, 0);
    gs_load_vertexbuffer(vbuf);
    gs_load_indexbuffer(nullptr);

    for(auto channel = 0u; channel < (m_stereo ? 2u : 1u); ++channel)
    {
        auto vertpos = 0u;
        vbdata = gs_vertexbuffer_get_data(vbuf);

        for(auto i = 0; i < m_num_bars; ++i)
        {
            auto x1 = (float)(i * bar_stride) + 0.5f;
            auto x2 = x1 + m_bar_width;
            auto val = m_interp_bufs[channel][i];

            if((m_display_mode == DisplayMode::STEPPED_BAR) || (m_display_mode == DisplayMode::STEPPED_METER))
            {
                for(auto j = 0u; j < max_steps; ++j)
                {
                    auto y1 = (float)(j * step_stride);
                    auto y2 = y1 + m_step_width;
                    if((cpos - val) < y2)
                        break;
                    if(channel)
                    {
                        y1 = cpos + y1;
                        y2 = cpos + y2;
                    }
                    else
                    {
                        y1 = cpos - y1;
                        y2 = cpos - y2;
                    }
                    vec3_set(&vbdata->points[vertpos], x1, y1, 0);
                    vec3_set(&vbdata->points[vertpos + 1], x2, y1, 0);
                    vec3_set(&vbdata->points[vertpos + 2], x1, y2, 0);
                    vec3_set(&vbdata->points[vertpos + 3], x2, y1, 0);
                    vec3_set(&vbdata->points[vertpos + 4], x1, y2, 0);
                    vec3_set(&vbdata->points[vertpos + 5], x2, y2, 0);
                    vertpos += 6;
                }
            }
            else
            {
                if(channel)
                    val = bottom - val;
                auto bot = (m_rounded_caps && !m_stereo) ? cpos - m_cap_radius : cpos;
                vec3_set(&vbdata->points[vertpos], x1, val, 0);
                vec3_set(&vbdata->points[vertpos + 1], x2, val, 0);
                vec3_set(&vbdata->points[vertpos + 2], x1, bot, 0);
                vec3_set(&vbdata->points[vertpos + 3], x2, val, 0);
                vec3_set(&vbdata->points[vertpos + 4], x1, bot, 0);
                vec3_set(&vbdata->points[vertpos + 5], x2, bot, 0);
                vertpos += 6;

                if(m_rounded_caps)
                {
                    auto ccx = (float)(i * bar_stride) + m_cap_radius; // cap center x
                    for(auto j = 0; j < m_cap_tris; ++j)
                    {
                        auto cx1 = m_cap_verts[j].x;
                        auto cy1 = m_cap_verts[j].y;
                        auto cx2 = m_cap_verts[j + 1].x;
                        auto cy2 = m_cap_verts[j + 1].y;
                        vec3_set(&vbdata->points[vertpos], cx1 + ccx, cy1 + val, 0);
                        vec3_set(&vbdata->points[vertpos + 1], cx2 + ccx, cy2 + val, 0);
                        vec3_set(&vbdata->points[vertpos + 2], ccx, val, 0);
                        vertpos += 3;
                    }
                    if(!m_stereo)
                    {
                        auto ccy = cpos - m_cap_radius;
                        for(auto j = 0; j < m_cap_tris; ++j)
                        {
                            auto cx1 = m_cap_verts[j].x;
                            auto cy1 = m_cap_verts[j].y;
                            auto cx2 = m_cap_verts[j + 1].x;
                            auto cy2 = m_cap_verts[j + 1].y;
                            vec3_set(&vbdata->points[vertpos], cx1 + ccx, cy1 + ccy, 0);
                            vec3_set(&vbdata->points[vertpos + 1], cx2 + ccx, cy2 + ccy, 0);
                            vec3_set(&vbdata->points[vertpos + 2], ccx, ccy, 0);
                            vertpos += 3;
                        }
                    }
                }
            }
        }

        gs_vertexbuffer_flush(vbuf);

        if(vertpos > 0)
            gs_draw(GS_TRIS, 0, vertpos);
    }

    gs_load_vertexbuffer(nullptr);
    gs_vertexbuffer_destroy(vbuf);
    gs_technique_end_pass(tech);
    gs_technique_end(tech);

    gs_effect_destroy(shader);
}

// WAVSourceAVX and WAVSourceAVX2 both inherit this implementation
// WAVSourceSSE2 overrides in source_sse2.cpp
DECORATE_AVX
void WAVSource::tick_meter(float seconds)
{
    if(!check_audio_capture(seconds))
        return;

    if(m_capture_channels == 0)
        return;

    // repurpose m_decibels as circular buffer for sample data
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
            constexpr auto step = sizeof(__m256) / sizeof(float);
            auto sum = _mm256_setzero_ps();
            for(size_t i = 0; i < m_fft_size; i += step)
            {
                auto chunk = _mm256_load_ps(&m_decibels[channel][i]);
                sum = _mm256_fmadd_ps(chunk, chunk, sum);
            }

            auto high = _mm256_extractf128_ps(sum, 1); // split into two 128-bit vecs
            auto low = _mm_add_ps(high, _mm256_castps256_ps128(sum)); // (h[0] + l[0]) (h[1] + l[1]) (h[2] + l[2]) (h[3] + l[3])
            high = _mm_permute_ps(low, _MM_SHUFFLE(3, 2, 3, 2)); // high[0] = low[2], high[1] = low[3]
            low = _mm_add_ps(high, low); // (h[0] + l[0]) (h[1] + l[1])
            high = _mm_movehdup_ps(low); // high[0] = low[1]
            out = _mm_cvtss_f32(_mm_add_ss(high, low));

            out = std::sqrt(out / m_fft_size);
        }
        else
        {
            constexpr auto step = sizeof(__m256) / sizeof(float);
            const auto signbit = _mm256_set1_ps(-0.0f);
            for(size_t i = 0; i < m_fft_size; i += step)
            {
                auto chunk = _mm256_andnot_ps(signbit, _mm256_load_ps(&m_decibels[channel][i])); // absolute value
                auto high = _mm256_extractf128_ps(chunk, 1); // split into two 128-bit vecs
                auto low = _mm_max_ps(high, _mm256_castps256_ps128(chunk)); // max(h[0], l[0]) max(h[1], l[1]) max(h[2], l[2]) max(h[3], l[3])
                high = _mm_permute_ps(low, _MM_SHUFFLE(3, 2, 3, 2)); // high[0] = low[2], high[1] = low[3]
                low = _mm_max_ps(high, low); // max(h[0], l[0]) max(h[1], l[1])
                high = _mm_movehdup_ps(low); // high[0] = low[1]
                auto max = _mm_cvtss_f32(_mm_max_ss(high, low));
                if(max > out)
                    out = max;
            }
        }

        const auto g = m_gravity;
        const auto g2 = 1.0f - g;
        if(m_tsmoothing == TSmoothingMode::EXPONENTIAL)
        {
            if(!m_fast_peaks || (out <= m_meter_buf[channel]))
                out = (g * m_meter_buf[channel]) + (g2 * out);
        }
        m_meter_buf[channel] = out;
        m_meter_val[channel] = dbfs(out);
    }
}

void WAVSource::show()
{
    std::lock_guard lock(m_mtx);
    m_show = true;
}

void WAVSource::hide()
{
    std::lock_guard lock(m_mtx);
    m_show = false;
}

void WAVSource::register_source()
{
    std::string arch;
    if(HAVE_AVX2)
        arch += " AVX2";
    if(HAVE_AVX)
        arch += " AVX";
    if(HAVE_FMA3)
        arch += " FMA3";
    arch += " SSE2";
#if defined(__x86_64__) || defined(_M_X64)
    blog(LOG_INFO, "[" MODULE_NAME "]: Registered v%s 64-bit", VERSION_STRING);
#elif defined(__i386__) || defined(_M_IX86)
    blog(LOG_INFO, "[" MODULE_NAME "]: Registered v%s 32-bit", VERSION_STRING);
#else
    blog(LOG_INFO, "[" MODULE_NAME "]: Registered v%s Unknown Arch", VERSION_STRING);
#endif
    blog(LOG_INFO, "[" MODULE_NAME "]: Using CPU capabilities:%s", arch.c_str());

    obs_source_info info{};
    info.id = MODULE_NAME "_source";
    info.type = OBS_SOURCE_TYPE_INPUT;
    info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
    info.get_name = &callbacks::get_name;
    info.create = &callbacks::create;
    info.destroy = &callbacks::destroy;
    info.get_width = &callbacks::get_width;
    info.get_height = &callbacks::get_height;
    info.get_defaults = &callbacks::get_defaults;
    info.get_properties = &callbacks::get_properties;
    info.update = &callbacks::update;
    info.show = &callbacks::show;
    info.hide = &callbacks::hide;
    info.video_tick = &callbacks::tick;
    info.video_render = &callbacks::render;
    info.icon_type = OBS_ICON_TYPE_AUDIO_OUTPUT;

    obs_register_source(&info);
}

void WAVSource::capture_audio([[maybe_unused]] obs_source_t *source, const audio_data *audio, bool muted)
{
    if(!m_mtx.try_lock_for(std::chrono::milliseconds(10)))
        return;
    std::lock_guard lock(m_mtx, std::adopt_lock);
    if(m_audio_source == nullptr)
        return;

    auto sz = size_t(audio->frames * sizeof(float));
    for(auto i = 0u; i < m_capture_channels; ++i)
    {
        if(muted)
            circlebuf_push_back_zero(&m_capturebufs[i], sz);
        else
            circlebuf_push_back(&m_capturebufs[i], audio->data[i], sz);

        auto total = m_capturebufs[i].size;
        auto max = m_meter_mode ? 4096 : m_fft_size * sizeof(float) * 2;
        if(total > max)
            circlebuf_pop_front(&m_capturebufs[i], nullptr, total - max);
    }
}

void WAVSource::capture_output_bus([[maybe_unused]] size_t mix_idx, const audio_data *audio)
{
    if(!m_mtx.try_lock_for(std::chrono::milliseconds(10)))
        return;
    std::lock_guard lock(m_mtx, std::adopt_lock);

    auto sz = size_t(audio->frames * sizeof(float));
    for(auto i = 0u; i < m_capture_channels; ++i)
    {
        circlebuf_push_back(&m_capturebufs[i], audio->data[i], sz);

        auto total = m_capturebufs[i].size;
        auto max = m_meter_mode ? 4096 : m_fft_size * sizeof(float) * 2;
        if(total > max)
            circlebuf_pop_front(&m_capturebufs[i], nullptr, total - max);
    }
}
