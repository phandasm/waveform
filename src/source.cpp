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
#include "log.hpp"
#include <vector>
#include <string>
#include <algorithm>
#include <limits>
#include <cassert>
#include <numbers>
#include <util/platform.h>
#include <utility>

#ifndef DISABLE_X86_SIMD

#include "cpuinfo_x86.h"

static const auto CPU_INFO = cpu_features::GetX86Info();
const bool WAVSource::HAVE_AVX2 = CPU_INFO.features.avx2 && CPU_INFO.features.fma3;
const bool WAVSource::HAVE_AVX = CPU_INFO.features.avx && CPU_INFO.features.fma3;
const bool WAVSource::HAVE_FMA3 = CPU_INFO.features.fma3;

#endif // !DISABLE_X86_SIMD

const float WAVSource::DB_MIN = 20.0f * std::log10(std::numeric_limits<float>::min());

#ifndef HAVE_OBS_PROP_ALPHA
#define obs_properties_add_color_alpha obs_properties_add_color
#endif

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
        LogWarn << "Could not determine audio configuration";
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
#ifndef DISABLE_X86_SIMD
        WAVSource *obj;
        if(WAVSource::HAVE_AVX2)
            obj = new WAVSourceAVX2(source);
        else if(WAVSource::HAVE_AVX)
            obj = new WAVSourceAVX(source);
        else
            obj = new WAVSourceGeneric(source);
#else
        WAVSource *obj = new WAVSourceGeneric(source);
#endif // !DISABLE_X86_SIMD
        obj->update(settings); // must be fully constructed before calling update()
        return static_cast<void*>(obj);
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
        obs_data_set_default_bool(settings, P_MIRROR_FREQ_AXIS, false);
        obs_data_set_default_bool(settings, P_RADIAL, false);
        obs_data_set_default_bool(settings, P_INVERT, false);
        obs_data_set_default_double(settings, P_DEADZONE, 20.0);
        obs_data_set_default_double(settings, P_RADIAL_ARC, 360.0);
        obs_data_set_default_double(settings, P_RADIAL_ROTATION, 0.0);
        obs_data_set_default_bool(settings, P_CAPS, false);
        obs_data_set_default_string(settings, P_CHANNEL_MODE, P_MONO);
        obs_data_set_default_int(settings, P_CHANNEL, 0);
        obs_data_set_default_int(settings, P_CHANNEL_SPACING, 0);
        obs_data_set_default_int(settings, P_FFT_SIZE, 4096);
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
        obs_data_set_default_double(settings, P_ROLLOFF_Q, 0.0);
        obs_data_set_default_double(settings, P_ROLLOFF_RATE, 0.0);
        obs_data_set_default_string(settings, P_RENDER_MODE, P_SOLID);
        obs_data_set_default_int(settings, P_COLOR_BASE, 0xffffffff);
        obs_data_set_default_int(settings, P_COLOR_CREST, 0xffffffff);
        obs_data_set_default_double(settings, P_GRAD_RATIO, 0.75);
        obs_data_set_default_int(settings, P_BAR_WIDTH, 24);
        obs_data_set_default_int(settings, P_BAR_GAP, 6);
        obs_data_set_default_int(settings, P_STEP_WIDTH, 8);
        obs_data_set_default_int(settings, P_STEP_GAP, 4);
        obs_data_set_default_int(settings, P_MIN_BAR_HEIGHT, 0);
        obs_data_set_default_int(settings, P_METER_BUF, 150);
        obs_data_set_default_bool(settings, P_RMS_MODE, true);
        obs_data_set_default_bool(settings, P_HIDE_SILENT, false);
        obs_data_set_default_bool(settings, P_IGNORE_MUTE, false);
        obs_data_set_default_bool(settings, P_NORMALIZE_VOLUME, false);
        obs_data_set_default_int(settings, P_VOLUME_TARGET, -8);
        obs_data_set_default_int(settings, P_MAX_GAIN, 30);
    }

    static obs_properties_t *get_properties([[maybe_unused]] void *data)
    {
        auto props = obs_properties_create();

        // audio source
        auto srclist = obs_properties_add_list(props, P_AUDIO_SRC, T(P_AUDIO_SRC), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
        obs_property_list_add_string(srclist, T(P_NONE), P_NONE);
        obs_property_list_add_string(srclist, T(P_OUTPUT_BUS), P_OUTPUT_BUS);
        obs_property_set_modified_callback(srclist, [](obs_properties_t *props, [[maybe_unused]] obs_property_t *property, obs_data_t *settings) -> bool {
            auto src = obs_data_get_string(settings, P_AUDIO_SRC);
            auto enable = (src == nullptr) || !p_equ(src, P_OUTPUT_BUS);
            set_prop_visible(props, P_IGNORE_MUTE, enable);
            return true;
            });

        for(const auto& str : enumerate_audio_sources())
            obs_property_list_add_string(srclist, str.c_str(), str.c_str());

        // hide on silent audio
        obs_properties_add_bool(props, P_HIDE_SILENT, T(P_HIDE_SILENT));

        // ignore mute
        auto ignore_mute = obs_properties_add_bool(props, P_IGNORE_MUTE, T(P_IGNORE_MUTE));
        obs_property_set_long_description(ignore_mute, T(P_IGNORE_MUTE_DESC));

        // volume normalization
        auto vol = obs_properties_add_bool(props, P_NORMALIZE_VOLUME, T(P_NORMALIZE_VOLUME));
        auto target = obs_properties_add_int_slider(props, P_VOLUME_TARGET, T(P_VOLUME_TARGET), -60, 0, 1);
        auto maxgain = obs_properties_add_int_slider(props, P_MAX_GAIN, T(P_MAX_GAIN), 0, 45, 1);
        obs_property_int_set_suffix(target, " dBFS");
        obs_property_int_set_suffix(maxgain, " dB");
        obs_property_set_long_description(vol, T(P_VOLUME_NORM_DESC));
        obs_property_set_modified_callback(vol, [](obs_properties_t *props, [[maybe_unused]] obs_property_t *property, obs_data_t *settings) -> bool {
            auto enable = obs_data_get_bool(settings, P_NORMALIZE_VOLUME) && obs_property_visible(obs_properties_get(props, P_NORMALIZE_VOLUME));
            set_prop_visible(props, P_VOLUME_TARGET, enable);
            set_prop_visible(props, P_MAX_GAIN, enable);
            return true;
            });

        // display type
        auto displaylist = obs_properties_add_list(props, P_DISPLAY_MODE, T(P_DISPLAY_MODE), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
        obs_property_list_add_string(displaylist, T(P_CURVE), P_CURVE);
        obs_property_list_add_string(displaylist, T(P_BARS), P_BARS);
        obs_property_list_add_string(displaylist, T(P_STEP_BARS), P_STEP_BARS);
        obs_property_list_add_string(displaylist, T(P_LEVEL_METER), P_LEVEL_METER);
        obs_property_list_add_string(displaylist, T(P_STEPPED_METER), P_STEPPED_METER);
        obs_property_list_add_string(displaylist, T(P_WAVEFORM), P_WAVEFORM);
        obs_properties_add_int(props, P_BAR_WIDTH, T(P_BAR_WIDTH), 1, 256, 1);
        obs_properties_add_int(props, P_BAR_GAP, T(P_BAR_GAP), 0, 256, 1);
        obs_properties_add_int(props, P_STEP_WIDTH, T(P_STEP_WIDTH), 1, 256, 1);
        obs_properties_add_int(props, P_STEP_GAP, T(P_STEP_GAP), 0, 256, 1);
        obs_properties_add_int(props, P_MIN_BAR_HEIGHT, T(P_MIN_BAR_HEIGHT), 0, 1080, 1);
        obs_property_set_modified_callback(displaylist, [](obs_properties_t *props, [[maybe_unused]] obs_property_t *property, obs_data_t *settings) -> bool {
            auto disp = obs_data_get_string(settings, P_DISPLAY_MODE);
            auto meter = p_equ(disp, P_LEVEL_METER);
            auto step_meter = p_equ(disp, P_STEPPED_METER);
            auto bar = p_equ(disp, P_BARS) || meter;
            auto step = p_equ(disp, P_STEP_BARS) || step_meter;
            auto curve = p_equ(disp, P_CURVE);
            auto waveform = p_equ(disp, P_WAVEFORM);
            set_prop_visible(props, P_BAR_WIDTH, bar || step);
            set_prop_visible(props, P_BAR_GAP, bar || step);
            set_prop_visible(props, P_STEP_WIDTH, step);
            set_prop_visible(props, P_STEP_GAP, step);
            set_prop_visible(props, P_MIN_BAR_HEIGHT, bar || step);
            set_prop_visible(props, P_CAPS, bar);
            obs_property_list_item_disable(obs_properties_get(props, P_RENDER_MODE), 0, !curve && !waveform);
            obs_property_list_item_disable(obs_properties_get(props, P_PULSE_MODE), 1, !curve && !p_equ(disp, P_BARS) && !p_equ(disp, P_STEP_BARS));

            // meter mode
            bool notmeter = !(meter || step_meter);
            set_prop_visible(props, P_SLOPE, notmeter && !waveform);
            set_prop_visible(props, P_ROLLOFF_Q, notmeter && !waveform);
            set_prop_visible(props, P_ROLLOFF_RATE, notmeter && !waveform);
            set_prop_visible(props, P_CUTOFF_LOW, notmeter && !waveform);
            set_prop_visible(props, P_CUTOFF_HIGH, notmeter && !waveform);
            set_prop_visible(props, P_FILTER_MODE, notmeter);
            set_prop_visible(props, P_FILTER_RADIUS, notmeter && !p_equ(obs_data_get_string(settings, P_FILTER_MODE), P_NONE));
            set_prop_visible(props, P_INTERP_MODE, notmeter);
            set_prop_visible(props, P_CHANNEL_MODE, notmeter);
            set_prop_visible(props, P_CHANNEL, notmeter && p_equ(obs_data_get_string(settings, P_CHANNEL_MODE), P_SINGLE));
            set_prop_visible(props, P_CHANNEL_SPACING, notmeter && p_equ(obs_data_get_string(settings, P_CHANNEL_MODE), P_STEREO));
            set_prop_visible(props, P_WINDOW, notmeter && !waveform);
            set_prop_visible(props, P_TSMOOTHING, !waveform);
            set_prop_visible(props, P_GRAVITY, !waveform && !p_equ(obs_data_get_string(settings, P_TSMOOTHING), P_NONE));
            set_prop_visible(props, P_FAST_PEAKS, !waveform && !p_equ(obs_data_get_string(settings, P_TSMOOTHING), P_NONE));
            set_prop_visible(props, P_RADIAL, notmeter);
            set_prop_visible(props, P_DEADZONE, notmeter && obs_data_get_bool(settings, P_RADIAL));
            set_prop_visible(props, P_RADIAL_ARC, notmeter && obs_data_get_bool(settings, P_RADIAL));
            set_prop_visible(props, P_RADIAL_ROTATION, notmeter && obs_data_get_bool(settings, P_RADIAL));
            set_prop_visible(props, P_INVERT, notmeter && obs_data_get_bool(settings, P_RADIAL));
            set_prop_visible(props, P_LOG_SCALE, notmeter && !waveform);
            set_prop_visible(props, P_MIRROR_FREQ_AXIS, notmeter && !waveform);
            set_prop_visible(props, P_WIDTH, notmeter);
            set_prop_visible(props, P_AUTO_FFT_SIZE, notmeter && !waveform);
            set_prop_visible(props, P_FFT_SIZE, notmeter && !waveform);
            set_prop_visible(props, P_RMS_MODE, !notmeter);
            set_prop_visible(props, P_METER_BUF, !notmeter || waveform);
            set_prop_visible(props, P_NORMALIZE_VOLUME, notmeter);
            set_prop_visible(props, P_VOLUME_TARGET, notmeter && obs_data_get_bool(settings, P_NORMALIZE_VOLUME));
            return true;
            });

        // video size
        obs_properties_add_int(props, P_WIDTH, T(P_WIDTH), 32, 3840, 1);
        obs_properties_add_int(props, P_HEIGHT, T(P_HEIGHT), 32, 2160, 1);

        // log scale
        obs_properties_add_bool(props, P_LOG_SCALE, T(P_LOG_SCALE));

        // mirror frequency axis
        auto mirror = obs_properties_add_bool(props, P_MIRROR_FREQ_AXIS, T(P_MIRROR_FREQ_AXIS));
        obs_property_set_long_description(mirror, T(P_MIRROR_DESC));

        // radial layout
        auto rad = obs_properties_add_bool(props, P_RADIAL, T(P_RADIAL));
        obs_properties_add_bool(props, P_INVERT, T(P_INVERT));
        auto deadzone = obs_properties_add_float_slider(props, P_DEADZONE, T(P_DEADZONE), 0.0, 100.0, 0.1);
        auto arc = obs_properties_add_float_slider(props, P_RADIAL_ARC, T(P_RADIAL_ARC), 0.0, 360.0, 0.1);
        auto rot = obs_properties_add_float_slider(props, P_RADIAL_ROTATION, T(P_RADIAL_ROTATION), 0.0, 360.0, 0.1);
        obs_property_float_set_suffix(deadzone, "%");
        obs_property_set_long_description(deadzone, T(P_DEADZONE_DESC));
        obs_property_float_set_suffix(arc, "°");
        obs_property_float_set_suffix(rot, "°");
        obs_property_set_long_description(arc, T(P_RADIAL_ARC_DESC));
        obs_property_set_modified_callback(rad, [](obs_properties_t *props, [[maybe_unused]] obs_property_t *property, obs_data_t *settings) -> bool {
            auto enable = obs_data_get_bool(settings, P_RADIAL) && obs_property_visible(obs_properties_get(props, P_RADIAL));
            set_prop_visible(props, P_DEADZONE, enable);
            set_prop_visible(props, P_RADIAL_ARC, enable);
            set_prop_visible(props, P_RADIAL_ROTATION, enable);
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
        obs_property_list_add_string(chanlst, T(P_SINGLE), P_SINGLE);
        obs_property_set_long_description(chanlst, T(P_CHAN_DESC));

        obs_properties_add_int(props, P_CHANNEL, T(P_CHANNEL), 0, MAX_AUDIO_CHANNELS - 1, 1);

        // channel spacing
        obs_properties_add_int(props, P_CHANNEL_SPACING, T(P_CHANNEL_SPACING), 0, 2160, 1);
        obs_property_set_modified_callback(chanlst, [](obs_properties_t *props, [[maybe_unused]] obs_property_t *property, obs_data_t *settings) -> bool {
            auto vis = obs_property_visible(obs_properties_get(props, P_CHANNEL_MODE));
            auto enable_spacing = p_equ(obs_data_get_string(settings, P_CHANNEL_MODE), P_STEREO) && vis;
            auto enable_channel = p_equ(obs_data_get_string(settings, P_CHANNEL_MODE), P_SINGLE) && vis;
            set_prop_visible(props, P_CHANNEL_SPACING, enable_spacing);
            set_prop_visible(props, P_CHANNEL, enable_channel);
            return true;
            });

        // fft size
        auto autofftsz = obs_properties_add_bool(props, P_AUTO_FFT_SIZE, T(P_AUTO_FFT_SIZE));
        auto fftsz = obs_properties_add_int_slider(props, P_FFT_SIZE, T(P_FFT_SIZE), 128, 8192, 64);
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
            auto enable = !p_equ(obs_data_get_string(settings, P_TSMOOTHING), P_NONE) && obs_property_visible(obs_properties_get(props, P_TSMOOTHING));
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
        auto rolloff_q = obs_properties_add_float_slider(props, P_ROLLOFF_Q, T(P_ROLLOFF_Q), 0.0, 10.0, 0.01);
        obs_property_set_long_description(rolloff_q, T(P_ROLLOFF_Q_DESC));
        auto rolloff_rate = obs_properties_add_float_slider(props, P_ROLLOFF_RATE, T(P_ROLLOFF_RATE), 0.0, 65.0, 0.01);
        obs_property_set_long_description(rolloff_rate, T(P_ROLLOFF_RATE_DESC));
        auto renderlist = obs_properties_add_list(props, P_RENDER_MODE, T(P_RENDER_MODE), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
        obs_property_list_add_string(renderlist, T(P_LINE), P_LINE);
        obs_property_list_add_string(renderlist, T(P_SOLID), P_SOLID);
        obs_property_list_add_string(renderlist, T(P_GRADIENT), P_GRADIENT);
        obs_property_list_add_string(renderlist, T(P_PULSE), P_PULSE);
        auto pulselist = obs_properties_add_list(props, P_PULSE_MODE, T(P_PULSE_MODE), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
        obs_property_list_add_string(pulselist, T(P_PEAK_MAG), P_PEAK_MAG);
        obs_property_list_add_string(pulselist, T(P_PEAK_FREQ), P_PEAK_FREQ);
        obs_properties_add_color_alpha(props, P_COLOR_BASE, T(P_COLOR_BASE));
        obs_properties_add_color_alpha(props, P_COLOR_CREST, T(P_COLOR_CREST));
        obs_properties_add_float_slider(props, P_GRAD_RATIO, T(P_GRAD_RATIO), 0.0, 4.0, 0.01);
        obs_property_set_modified_callback(renderlist, [](obs_properties_t *props, [[maybe_unused]] obs_property_t *property, obs_data_t *settings) -> bool {
            auto grad = p_equ(obs_data_get_string(settings, P_RENDER_MODE), P_GRADIENT);
            auto pulse = p_equ(obs_data_get_string(settings, P_RENDER_MODE), P_PULSE);
            obs_property_set_enabled(obs_properties_get(props, P_COLOR_CREST), grad || pulse);
            set_prop_visible(props, P_GRAD_RATIO, grad || pulse);
            set_prop_visible(props, P_PULSE_MODE, pulse);
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
    m_mirror_freq_axis = obs_data_get_bool(settings, P_MIRROR_FREQ_AXIS);
    m_radial = obs_data_get_bool(settings, P_RADIAL);
    m_invert = obs_data_get_bool(settings, P_INVERT);
    auto deadzone = (float)obs_data_get_double(settings, P_DEADZONE) / 100.0f;
    m_radial_arc = (float)obs_data_get_double(settings, P_RADIAL_ARC) / 360.0f;
    m_radial_rotation = ((float)obs_data_get_double(settings, P_RADIAL_ROTATION) / 360.0f) * (std::numbers::pi_v<float> * 2);
    m_rounded_caps = obs_data_get_bool(settings, P_CAPS);
    auto channel_mode = obs_data_get_string(settings, P_CHANNEL_MODE);
    m_stereo = p_equ(channel_mode, P_STEREO);
    m_channel_base = (int)obs_data_get_int(settings, P_CHANNEL);
    m_channel_spacing = (int)obs_data_get_int(settings, P_CHANNEL_SPACING);
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
    m_rolloff_q = (float)obs_data_get_double(settings, P_ROLLOFF_Q);
    m_rolloff_rate = (float)obs_data_get_double(settings, P_ROLLOFF_RATE);
    auto rendermode = obs_data_get_string(settings, P_RENDER_MODE);
    auto pulsemode = obs_data_get_string(settings, P_PULSE_MODE);
    auto color_base = obs_data_get_int(settings, P_COLOR_BASE);
    auto color_crest = obs_data_get_int(settings, P_COLOR_CREST);
    m_grad_ratio = (float)obs_data_get_double(settings, P_GRAD_RATIO);
    auto display = obs_data_get_string(settings, P_DISPLAY_MODE);
    m_bar_width = (int)obs_data_get_int(settings, P_BAR_WIDTH);
    m_bar_gap = (int)obs_data_get_int(settings, P_BAR_GAP);
    m_step_width = (int)obs_data_get_int(settings, P_STEP_WIDTH);
    m_step_gap = (int)obs_data_get_int(settings, P_STEP_GAP);
    m_min_bar_height = (int)obs_data_get_int(settings, P_MIN_BAR_HEIGHT);
    m_meter_rms = obs_data_get_bool(settings, P_RMS_MODE);
    m_meter_ms = (int)obs_data_get_int(settings, P_METER_BUF);
    m_hide_on_silent = obs_data_get_bool(settings, P_HIDE_SILENT);
    m_ignore_mute = obs_data_get_bool(settings, P_IGNORE_MUTE);
    m_normalize_volume = obs_data_get_bool(settings, P_NORMALIZE_VOLUME);
    m_volume_target = (float)obs_data_get_int(settings, P_VOLUME_TARGET);
    m_max_gain = (float)obs_data_get_int(settings, P_MAX_GAIN);

    m_color_base = { {{(uint8_t)color_base / 255.0f, (uint8_t)(color_base >> 8) / 255.0f, (uint8_t)(color_base >> 16) / 255.0f, (uint8_t)(color_base >> 24) / 255.0f}} };
    m_color_crest = { {{(uint8_t)color_crest / 255.0f, (uint8_t)(color_crest >> 8) / 255.0f, (uint8_t)(color_crest >> 16) / 255.0f, (uint8_t)(color_crest >> 24) / 255.0f}} };

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

    if(!m_stereo || (((int)m_height - m_channel_spacing) < 1))
        m_channel_spacing = 0;

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
    else if(p_equ(rendermode, P_GRADIENT))
        m_render_mode = RenderMode::GRADIENT;
    else if(p_equ(rendermode, P_PULSE))
        m_render_mode = RenderMode::PULSE;
    else
        m_render_mode = RenderMode::SOLID;

    if(p_equ(pulsemode, P_PEAK_FREQ))
        m_pulse_mode = PulseMode::FREQUENCY;
    else
        m_pulse_mode = PulseMode::MAGNITUDE;

    if(p_equ(display, P_BARS))
        m_display_mode = DisplayMode::BAR;
    else if(p_equ(display, P_STEP_BARS))
        m_display_mode = DisplayMode::STEPPED_BAR;
    else if(p_equ(display, P_LEVEL_METER))
        m_display_mode = DisplayMode::METER;
    else if(p_equ(display, P_STEPPED_METER))
        m_display_mode = DisplayMode::STEPPED_METER;
    else if(p_equ(display, P_WAVEFORM))
        m_display_mode = DisplayMode::WAVEFORM;
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

    if(!m_meter_mode && p_equ(channel_mode, P_SINGLE))
        m_channel_mode = ChannelMode::SINGLE;
    else if(p_equ(channel_mode, P_STEREO))
        m_channel_mode = ChannelMode::STEREO;
    else
        m_channel_mode = ChannelMode::MONO;
}

void WAVSource::recapture_audio()
{
    // release old capture
    release_audio_capture();

    // add new capture
    auto src_name = m_audio_source_name.c_str();
    if(p_equ(src_name, P_NONE))
        return;
    else if(p_equ(src_name, P_OUTPUT_BUS))
    {
        if(m_audio_info.speakers != speaker_layout::SPEAKERS_UNKNOWN)
        {
            auto audio = obs_get_audio();
            auto info = audio_output_get_info(audio);
            if((info->format == audio_format::AUDIO_FORMAT_FLOAT_PLANAR) && (info->samples_per_sec == m_audio_info.samples_per_sec) && (info->speakers == m_audio_info.speakers))
            {
                m_output_bus_captured = audio_output_connect(audio, 0, nullptr, &callbacks::capture_output_bus, this);
            }
            else
            {
                audio_convert_info cvt{};
                cvt.format = audio_format::AUDIO_FORMAT_FLOAT_PLANAR;
                cvt.samples_per_sec = m_audio_info.samples_per_sec;
                cvt.speakers = m_audio_info.speakers;
                m_output_bus_captured = audio_output_connect(audio, 0, &cvt, &callbacks::capture_output_bus, this);
            }
        }
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
        else
        {
            if(m_retries++ == 0)
                LogWarn << "Failed to get audio source: \"" << src_name << "\"";
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
        m_output_bus_captured = false;
        audio_output_disconnect(obs_get_audio(), 0, &callbacks::capture_output_bus, this);
    }

    // reset circular buffers
    for(auto& i : m_capturebufs)
    {
        i.end_pos = 0;
        i.start_pos = 0;
        i.size = 0;
    }
    m_rms_sync_buf.end_pos = 0;
    m_rms_sync_buf.start_pos = 0;
    m_rms_sync_buf.size = 0;

    m_capture_ts = 0;
    m_audio_ts = 0;
}

bool WAVSource::check_audio_capture(float seconds)
{
    if(m_output_bus_captured)
        return true;

    // check if the source still exists
    if(m_audio_source != nullptr)
    {
        auto src = obs_weak_source_get_source(m_audio_source);
        if(src == nullptr)
            release_audio_capture();
        else
            obs_source_release(src);
    }

    // if we've lost our source, periodically try to recapture it
    if(m_audio_source == nullptr)
    {
        m_next_retry -= seconds;
        if(m_next_retry <= 0.0f)
        {
            m_next_retry = RETRY_DELAY;
            recapture_audio();
            if((m_audio_source != nullptr) || m_output_bus_captured)
                return true;
        }
        return false;
    }
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
    m_input_rms_buf.reset();
    m_rms_temp_buf.reset();
    m_rolloff_modifiers.reset();

    m_kernel = {};
    m_lanczos_kernel = {};

    if(m_fft_plan != nullptr)
    {
        fftwf_destroy_plan(m_fft_plan);
        m_fft_plan = nullptr;
    }

    m_fft_size = 0;
}

bool WAVSource::sync_rms_buffer()
{
    const int64_t dtaudio = get_audio_sync(m_tick_ts);
    const size_t dtsize = (dtaudio > 0) ? size_t(ns_to_audio_frames(m_audio_info.samples_per_sec, (uint64_t)dtaudio)) * sizeof(float) : 0;

    if(m_rms_sync_buf.size <= dtsize)
        return false;

    while(m_rms_sync_buf.size > dtsize)
    {
        auto consume = m_rms_sync_buf.size - dtsize;
        auto max = (m_input_rms_size - m_input_rms_pos) * sizeof(float);
        if(consume >= max)
        {
            circlebuf_pop_front(&m_rms_sync_buf, &m_input_rms_buf[m_input_rms_pos], max);
            m_input_rms_pos = 0;
        }
        else
        {
            circlebuf_pop_front(&m_rms_sync_buf, &m_input_rms_buf[m_input_rms_pos], consume);
            m_input_rms_pos += consume / sizeof(float);
        }
    }

    return true;
}

void WAVSource::init_interp(unsigned int sz)
{
    const auto maxbin = (m_fft_size / 2) - 1;
    const auto sr = (float)m_audio_info.samples_per_sec;
    float lowbin, highbin;
    if(m_display_mode == DisplayMode::WAVEFORM)
    {
        lowbin = 0.0f;
        highbin = (float)(m_fft_size - 1);
    }
    else
    {
        lowbin = std::clamp((float)m_cutoff_low * m_fft_size / sr, 1.0f, (float)maxbin);
        highbin = std::clamp((float)m_cutoff_high * m_fft_size / sr, 1.0f, (float)maxbin);
    }

    m_interp_indices.resize(sz);
    if(m_log_scale)
    {
        for(auto i = 0u; i < sz; ++i)
            m_interp_indices[i] = std::clamp(log_interp(lowbin, highbin, (m_mirror_freq_axis ? i * 2.0f : (float)i) / (float)(sz - 1)), lowbin, highbin);
    }
    else
    {
        for(auto i = 0u; i < sz; ++i)
            m_interp_indices[i] = std::clamp(lerp(lowbin, highbin, (m_mirror_freq_axis ? i * 2.0f : (float)i) / (float)(sz - 1)), lowbin, highbin);
    }

    // bar bands
    if((m_display_mode == DisplayMode::BAR) || (m_display_mode == DisplayMode::STEPPED_BAR))
    {
        m_band_widths.resize(m_num_bars);
        for(auto i = 0; i < m_num_bars; ++i)
            m_band_widths[i] = std::max((int)(m_interp_indices[i + 1] - m_interp_indices[i]), 1);
    }

    // lanczos filter
    if(m_interp_mode == InterpMode::LANCZOS)
    {
        if((m_display_mode != DisplayMode::CURVE) && (m_display_mode != DisplayMode::WAVEFORM))
        {
            // at this point m_interp_indices only contains the start of each band
            // so we'll fill in the intermediate points here
            std::vector<float> samples;
            samples.reserve((size_t)std::ceil(highbin - lowbin));
            for(auto i = 0; i < m_num_bars; ++i)
            {
                auto count = m_band_widths[i];
                for(auto j = 0; j < count; ++j)
                    samples.push_back(m_interp_indices[i] + j);
            }
            m_interp_indices = std::move(samples);
        }
#ifndef DISABLE_X86_SIMD
        m_lanczos_kernel = make_lanczos_kernel(m_interp_indices, HAVE_AVX ? 4 : 3); // 3 is good enough, 4 for simd alignment
#else
        m_lanczos_kernel = make_lanczos_kernel(m_interp_indices, 3);
#endif
    }
}

void WAVSource::init_rolloff()
{
    const auto sz = m_fft_size / 2;
    const auto sr = (float)m_audio_info.samples_per_sec;
    const auto coeff = sr / (float)m_fft_size;
    const auto ratio = std::exp2(m_rolloff_q);
    const auto freq_low = (float)m_cutoff_low * ratio;
    const auto freq_high = (float)m_cutoff_high / ratio;

    m_rolloff_modifiers.reset(m_fft_size / 2);
    m_rolloff_modifiers[0] = 0.0f;
    for(size_t i = 1u; i < sz; ++i)
    {
        auto freq = i * coeff;
        auto ratio_low = freq_low / freq;
        auto ratio_high = freq / freq_high;
        auto low_attenuation = (ratio_low > 1.0f) ? (m_rolloff_rate * std::log2(ratio_low)) : 0.0f;
        auto high_attenuation = (ratio_high > 1.0f) ? (m_rolloff_rate * std::log2(ratio_high)) : 0.0f;
        m_rolloff_modifiers[i] = low_attenuation + high_attenuation;
    }
}

void WAVSource::init_steps()
{
    const auto x1 = 0.0f;
    const auto x2 = (float)m_bar_width;
    const auto y1 = 0.0f;
    const auto y2 = (float)m_step_width;

    vec3_set(&m_step_verts[0], x1, y1, 0);
    vec3_set(&m_step_verts[1], x2, y1, 0);
    vec3_set(&m_step_verts[2], x1, y2, 0);
    vec3_set(&m_step_verts[3], x2, y1, 0);
    vec3_set(&m_step_verts[4], x1, y2, 0);
    vec3_set(&m_step_verts[5], x2, y2, 0);
}

WAVSource::WAVSource(obs_source_t *source)
{
    m_source = source;
    for(auto& i : m_capturebufs)
        circlebuf_init(&i);
    circlebuf_init(&m_rms_sync_buf);

    obs_enter_graphics();

    // create shader
    auto filename = obs_module_file("gradient.effect");
    m_shader = gs_effect_create_from_file(filename, nullptr);
    bfree(filename);

    obs_leave_graphics();
}

WAVSource::~WAVSource()
{
    std::lock_guard lock(m_mtx);
    obs_enter_graphics();

    gs_vertexbuffer_destroy(m_vbuf);
    gs_effect_destroy(m_shader);

    obs_leave_graphics();

    release_audio_capture();
    free_bufs();

    for(auto& i : m_capturebufs)
        circlebuf_free(&i);
    circlebuf_free(&m_rms_sync_buf);
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

void WAVSource::create_vbuf() {
    size_t num_verts = 0;
    bool curve = (m_display_mode == DisplayMode::CURVE) || (m_display_mode == DisplayMode::WAVEFORM);

    if(curve)
        num_verts = (size_t)((m_render_mode == RenderMode::LINE) ? m_width : (m_width * 2));
    else
    {
        const auto step_stride = m_step_width + m_step_gap;
        const auto center = (float)m_height / 2;
        const auto bottom = (float)m_height;
        const auto cpos = m_stereo ? center : bottom;
        const auto channel_offset = m_channel_spacing * 0.5f;

        auto max_steps = (size_t)((cpos - channel_offset) / step_stride);
        if(((int)cpos - (int)(max_steps * step_stride) - (int)channel_offset) > m_step_width)
            ++max_steps;

        num_verts = (size_t)(m_num_bars * 6);
        if((m_display_mode == DisplayMode::STEPPED_BAR) || (m_display_mode == DisplayMode::STEPPED_METER))
            num_verts *= max_steps;
        else if(m_rounded_caps)
            num_verts += m_cap_tris * ((m_channel_spacing > 0) ? 12 : 6) * m_num_bars; // 2 caps per bar (middle omitted when 0 spacing)
    }

    obs_enter_graphics();

    gs_vertexbuffer_destroy(m_vbuf);

    auto vbdata = gs_vbdata_create();
    vbdata->num = num_verts;
    vbdata->points = (vec3*)bmalloc(num_verts * sizeof(vec3));
    vbdata->num_tex = 1;
    vbdata->tvarray = (gs_tvertarray*)bzalloc(sizeof(gs_tvertarray));
    vbdata->tvarray->width = 2;
    vbdata->tvarray->array = bmalloc(2 * num_verts * sizeof(float));
    m_vbuf = gs_vertexbuffer_create(vbdata, GS_DYNAMIC);

    if(curve) {
        if(m_render_mode == RenderMode::LINE)
        {
            for(auto i = 0u; i < m_width; ++i)
                vec3_set(&vbdata->points[i], (float)i, 0, 0);
        }
        else
        {
            for(auto i = 0u; i < m_width; ++i)
            {
                vec3_set(&vbdata->points[i * 2], (float)i, 0, 0);
                vec3_set(&vbdata->points[(i * 2) + 1], (float)i, 0, 0);
            }
        }
    }

    obs_leave_graphics();
}

void WAVSource::update(obs_data_t *settings)
{
    std::lock_guard lock(m_mtx);
    constexpr auto pi = std::numbers::pi_v<float>;

    release_audio_capture();
    free_bufs();
    get_settings(settings);

    // get current audio settings
    update_audio_info(&m_audio_info);
    const auto max_channels = get_audio_channels(m_audio_info.speakers);
    m_capture_channels = std::min(max_channels, 2u);
    if(m_capture_channels == 0)
        LogWarn << "Unknown channel config: " << (unsigned int)m_audio_info.speakers;
    if(m_channel_mode == ChannelMode::SINGLE)
    {
        if((m_channel_base < 0) || (m_channel_base >= (int)max_channels) || (m_channel_base >= MAX_AUDIO_CHANNELS))
        {
            m_capture_channels = 0;
            m_channel_base = 0;
        }
        else
            m_capture_channels = std::min(m_capture_channels, 1u);
    }
    else
        m_channel_base = 0;

    // meter mode
    if(m_meter_mode)
    {
        // turn off stuff we don't need in this mode
        m_window_func = FFTWindow::NONE;
        m_interp_mode = InterpMode::POINT;
        m_filter_mode = FilterMode::NONE;
        m_pulse_mode = PulseMode::MAGNITUDE;
        m_auto_fft_size = false;
        m_slope = 0.0f;
        m_stereo = false;
        m_radial = false;
        m_normalize_volume = false;
        m_mirror_freq_axis = false;

        // repurpose m_fft_size for meter buffer size
        m_fft_size = size_t(m_audio_info.samples_per_sec * (m_meter_ms / 1000.0)) & -16;

        memset(m_meter_pos, 0, sizeof(m_meter_pos));
        for(auto& i : m_meter_buf)
            i = DB_MIN;
        for(auto& i : m_meter_val)
            i = DB_MIN;
    }
    else if(m_display_mode == DisplayMode::WAVEFORM)
    {
        // turn off stuff we don't need in this mode
        m_window_func = FFTWindow::NONE;
        m_pulse_mode = PulseMode::MAGNITUDE;
        m_auto_fft_size = false;
        m_slope = 0.0f;
        m_mirror_freq_axis = false;
        m_log_scale = false;

        // repurpose m_fft_size for buffer size
        m_fft_size = size_t(m_audio_info.samples_per_sec * (m_meter_ms / 1000.0)) & -16;
    }

    if(m_normalize_volume)
    {
        m_input_rms = 0.0f;
        m_input_rms_size = size_t(m_audio_info.samples_per_sec) & -16;
        m_input_rms_pos = 0;
        m_input_rms_buf.reset(m_input_rms_size);
        m_rms_temp_buf.reset(AUDIO_OUTPUT_FRAMES);
        memset(m_input_rms_buf.get(), 0, m_input_rms_size * sizeof(float));
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
    auto spectrum_mode = !m_meter_mode && (m_display_mode != DisplayMode::WAVEFORM);
    m_output_channels = ((m_capture_channels > 1) || m_stereo) ? 2u : 1u;
    for(auto i = 0u; i < m_output_channels; ++i)
    {
        auto count = spectrum_mode ? m_fft_size / 2 : m_fft_size;
        m_decibels[i].reset(count);
        if(spectrum_mode && (m_tsmoothing != TSmoothingMode::NONE))
        {
            m_tsmooth_buf[i].reset(count);
            std::fill(m_tsmooth_buf[i].get(), m_tsmooth_buf[i].get() + count, 0.0f);
        }
        std::fill(m_decibels[i].get(), m_decibels[i].get() + count, m_meter_mode ? 0.0f : DB_MIN);
    }
    if(spectrum_mode)
    {
        m_fft_input.reset(m_fft_size);
        m_fft_output.reset(m_fft_size);
        m_fft_plan = fftwf_plan_dft_r2c_1d((int)m_fft_size, m_fft_input.get(), m_fft_output.get(), FFTW_ESTIMATE);
    }

    // window function
    if(m_window_func != FFTWindow::NONE)
    {
        // precompute window coefficients
        m_window_coefficients.reset(m_fft_size);
        const auto N = m_fft_size - 1;
        constexpr auto pi2 = 2 * pi;
        constexpr auto pi4 = 4 * pi;
        constexpr auto pi6 = 6 * pi;
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

        auto sum = 0.0f;
        for(size_t i = 0; i < m_fft_size; ++i)
            sum += m_window_coefficients[i];
        m_window_sum = sum;
    }
    else
        m_window_sum = (float)m_fft_size;

    m_last_silent = false;
    m_show = obs_source_showing(m_source);
    m_retries = 0;
    m_next_retry = 0.0f;

    recapture_audio();
    m_capture_ts = os_gettime_ns();

    // precomupte interpolated indices
    if((m_display_mode == DisplayMode::CURVE) || (m_display_mode == DisplayMode::WAVEFORM))
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
    if(m_slope > 0.0f)
    {
        const auto num_mods = m_fft_size / 2;
        const auto maxmod = (float)(num_mods - 1);
        m_slope_modifiers.reset(num_mods);
        for(size_t i = 0; i < num_mods; ++i)
            m_slope_modifiers[i] = std::log10(log_interp(10.0f, 10000.0f, ((float)i * m_slope) / maxmod));
    }

    // rounded caps
    m_cap_verts.clear();
    if(m_rounded_caps)
    {
        // caps are full circles to avoid distortion issues in radial mode
        m_cap_radius = (float)m_bar_width / 2.0f;
        m_cap_tris = std::max((int)((2 * pi * m_cap_radius) / 3.0f), 4);
        if(m_cap_tris & 1) // force even number of triangles
            m_cap_tris += 1;
        auto angle = (2 * pi) / (float)m_cap_tris;
        auto verts = m_cap_tris + 1;
        m_cap_verts.resize(verts);
        for(auto j = 0; j < verts; ++j)
        {
            auto a = j * angle;
            vec3_set(&m_cap_verts[j], m_cap_radius * std::cos(a), m_cap_radius * std::sin(a), 0.0f);
        }
    }

    // stepped bars
    if((m_display_mode == DisplayMode::STEPPED_BAR) || (m_display_mode == DisplayMode::STEPPED_METER))
        init_steps();

    // roll-off
    if((m_rolloff_q > 0.0f) && (m_rolloff_rate > 0.0f))
        init_rolloff();

    // vertex buffer must be rebuilt if the settings have changed
    // this must be done after m_num_bars has been initialized
    create_vbuf();
}

void WAVSource::tick(float seconds)
{
    std::lock_guard lock(m_mtx);

    m_tick_ts = os_gettime_ns();

    if(m_normalize_volume)
        update_input_rms();

    if(!check_audio_capture(seconds))
        return;
    if(m_capture_channels == 0)
        return;

    if(m_meter_mode)
        tick_meter(seconds);
    else if(m_display_mode == DisplayMode::WAVEFORM)
        tick_waveform(seconds);
    else
        tick_spectrum(seconds);
}

void WAVSource::render([[maybe_unused]] gs_effect_t *effect)
{
    std::lock_guard lock(m_mtx);
    if(m_last_silent && m_hide_on_silent)
        return;

    if((m_display_mode == DisplayMode::CURVE) || (m_display_mode == DisplayMode::WAVEFORM))
        render_curve(effect);
    else
        render_bars(effect);
}

void WAVSource::render_curve([[maybe_unused]] gs_effect_t *effect)
{
    //std::lock_guard lock(m_mtx); // now locked in render()
    //if(m_last_silent)
    //    return;

    auto tech = get_shader_tech();
    
    const auto center = (float)m_height / 2;
    //const auto right = (float)m_width;
    const auto bottom = (float)m_height;
    const auto dbrange = m_ceiling - m_floor;
    const auto cpos = m_stereo ? center : bottom;
    const auto channel_offset = m_channel_spacing * 0.5f;

    // interpolation
    auto miny = cpos;
    auto minpos = 0u;
    for(auto channel = 0u; channel < (m_stereo ? 2u : 1u); ++channel)
    {
        if(m_interp_mode == InterpMode::LANCZOS)
        {
            const auto sz = (m_display_mode == DisplayMode::WAVEFORM) ? m_fft_size : m_fft_size / 2u;
#ifndef DISABLE_X86_SIMD
            if(HAVE_AVX)
                apply_lanczos_filter_fma3(m_decibels[channel].get(), sz, m_interp_indices, m_lanczos_kernel, m_interp_bufs[channel]);
            else
                apply_lanczos_filter(m_decibels[channel].get(), sz, m_interp_indices, m_lanczos_kernel, m_interp_bufs[channel]);
#else
            apply_lanczos_filter(m_decibels[channel].get(), sz, m_interp_indices, m_lanczos_kernel, m_interp_bufs[channel]);
#endif
        }
        else
            for(auto i = 0u; i < m_width; ++i)
                m_interp_bufs[channel][i] = m_decibels[channel][(int)m_interp_indices[i]];

        if(m_filter_mode != FilterMode::NONE)
        {
#ifndef DISABLE_X86_SIMD
            if(HAVE_AVX)
                m_interp_bufs[channel] = apply_filter_fma3(m_interp_bufs[channel], m_kernel, m_interp_bufs[2]);
            else
                m_interp_bufs[channel] = apply_filter(m_interp_bufs[channel], m_kernel, m_interp_bufs[2]);
#else
            m_interp_bufs[channel] = apply_filter(m_interp_bufs[channel], m_kernel, m_interp_bufs[2]);
#endif // !DISABLE_X86_SIMD
        }
        
        for(auto i = 0u; i < m_width; ++i)
        {
            auto val = lerp(0.0f, cpos - channel_offset, std::clamp(m_ceiling - m_interp_bufs[channel][i], 0.0f, (float)dbrange) / dbrange);
            if(val < miny)
            {
                miny = val;
                minpos = i;
            }
            m_interp_bufs[channel][i] = val;
        }

        if(m_mirror_freq_axis)
        {
            const auto half = (m_width / 2u);
            for(auto i = half + 1; i < m_width; ++i)
                m_interp_bufs[channel][i] = m_interp_bufs[channel][half - (i - half)];
        }
    }

    set_shader_vars(cpos, miny, (float)minpos, channel_offset, 0.0f, cpos - channel_offset);

    gs_technique_begin(tech);
    gs_technique_begin_pass(tech, 0);
    gs_load_vertexbuffer(m_vbuf);
    gs_load_indexbuffer(nullptr);

    auto vbdata = gs_vertexbuffer_get_data(m_vbuf);

    for(auto channel = 0u; channel < (m_stereo ? 2u : 1u); ++channel)
    {
        auto offset = channel_offset;
        if(channel)
            offset = -offset;
        auto bot = cpos - offset;

        for(auto i = 0u; i < m_width; ++i)
        {
            auto val = m_interp_bufs[channel][i];
            if(m_render_mode == RenderMode::LINE)
            {
                if(channel == 0)
                    vbdata->points[i].y = val;
                else
                    vbdata->points[i].y = bottom - val;
            }
            else
            {
                if(channel == 0)
                    vbdata->points[i * 2].y = val;
                else
                    vbdata->points[i * 2].y = bottom - val;
                vbdata->points[(i * 2) + 1].y = bot;
            }
        }

        gs_vertexbuffer_flush(m_vbuf);

        gs_draw((m_render_mode != RenderMode::LINE) ? GS_TRISTRIP : GS_LINESTRIP, 0, (uint32_t)vbdata->num);
    }

    gs_load_vertexbuffer(nullptr);
    gs_technique_end_pass(tech);
    gs_technique_end(tech);
}

void WAVSource::render_bars([[maybe_unused]] gs_effect_t *effect)
{
    //std::lock_guard lock(m_mtx); // now locked in render()
    //if(m_last_silent)
    //    return;

    auto tech = get_shader_tech();

    const auto bar_stride = m_bar_width + m_bar_gap;
    const auto step_stride = m_step_width + m_step_gap;
    const auto center = (float)m_height / 2;
    const auto bottom = (float)m_height;
    const auto dbrange = m_ceiling - m_floor;
    const auto cpos = m_stereo ? center : bottom;
    const auto channel_offset = m_channel_spacing * 0.5f;
    auto border_top = (m_rounded_caps) ? m_cap_radius : 0.0f;
    auto border_bottom = (m_rounded_caps && (!m_stereo || (m_channel_spacing > 0))) ? cpos - m_cap_radius : cpos;
    if(m_channel_spacing > 0)
        border_bottom -= channel_offset;
    if(m_min_bar_height > 0)
        border_bottom -= m_min_bar_height;
    border_bottom = std::clamp(border_bottom, border_top, cpos);

    auto max_steps = (size_t)((cpos - channel_offset) / step_stride);
    if(((int)cpos - (int)(max_steps * step_stride) - (int)channel_offset) > m_step_width)
        ++max_steps;

    // interpolation
    auto miny = cpos;
    auto minpos = 0u;
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
#ifndef DISABLE_X86_SIMD
                if(HAVE_AVX)
                    apply_lanczos_filter_fma3(m_decibels[channel].get(), m_fft_size / 2, m_band_widths, m_interp_indices, m_lanczos_kernel, m_interp_bufs[channel]);
                else
                    apply_lanczos_filter(m_decibels[channel].get(), m_fft_size / 2, m_band_widths, m_interp_indices, m_lanczos_kernel, m_interp_bufs[channel]);
#else
                apply_lanczos_filter(m_decibels[channel].get(), m_fft_size / 2, m_band_widths, m_interp_indices, m_lanczos_kernel, m_interp_bufs[channel]);
#endif
            }
            else
            {
                for(auto i = 0; i < m_num_bars; ++i)
                {
                    float sum = 0.0f;
                    auto count = (size_t)m_band_widths[i];
                    for(size_t j = 0; j < count; ++j)
                        sum += m_decibels[channel][(size_t)m_interp_indices[i] + j];
                    m_interp_bufs[channel][i] = sum / (float)count;
                }
            }

            if(m_filter_mode != FilterMode::NONE)
            {
#ifndef DISABLE_X86_SIMD
                if(HAVE_AVX)
                    m_interp_bufs[channel] = apply_filter_fma3(m_interp_bufs[channel], m_kernel, m_interp_bufs[2]);
                else
                    m_interp_bufs[channel] = apply_filter(m_interp_bufs[channel], m_kernel, m_interp_bufs[2]);
#else
                m_interp_bufs[channel] = apply_filter(m_interp_bufs[channel], m_kernel, m_interp_bufs[2]);
#endif // !DISABLE_X86_SIMD
            }
        }

        for(auto i = 0; i < m_num_bars; ++i)
        {
            auto val = lerp(border_top, border_bottom, std::clamp(m_ceiling - m_interp_bufs[channel][i], 0.0f, (float)dbrange) / dbrange);
            if(val < miny)
            {
                miny = val;
                minpos = i;
            }
            m_interp_bufs[channel][i] = val;
        }

        if(m_mirror_freq_axis)
        {
            const auto half = (m_num_bars / 2u);
            for(auto i = half + 1; i < (unsigned int)m_num_bars; ++i)
                m_interp_bufs[channel][i] = m_interp_bufs[channel][half - (i - half)];
        }
    }

    set_shader_vars(cpos, miny, (float)minpos, channel_offset, border_top, border_bottom);

    gs_technique_begin(tech);
    gs_technique_begin_pass(tech, 0);
    gs_load_vertexbuffer(m_vbuf);
    gs_load_indexbuffer(nullptr);

    auto vbdata = gs_vertexbuffer_get_data(m_vbuf);

    for(auto channel = 0u; channel < (m_stereo ? 2u : 1u); ++channel)
    {
        auto vertpos = 0u;

        for(auto i = 0; i < m_num_bars; ++i)
        {
            auto val = m_interp_bufs[channel][i];

            if((m_display_mode == DisplayMode::STEPPED_BAR) || (m_display_mode == DisplayMode::STEPPED_METER))
            {
                const auto x = (float)(i * bar_stride);
                const auto maxheight = (cpos - val - channel_offset);
                for(auto j = 0u; j < max_steps; ++j)
                {
                    auto y = (float)(j * step_stride);
                    if(y >= maxheight)
                        break;
                    if(channel)
                        y = cpos + y + channel_offset;
                    else
                        y = cpos - y - channel_offset - m_step_width;

                    vec3 vert;
                    vec3_set(&vert, x, y, 0.0f);
                    vec3_add(&vbdata->points[vertpos], &m_step_verts[0], &vert);
                    vec3_add(&vbdata->points[vertpos + 1], &m_step_verts[1], &vert);
                    vec3_add(&vbdata->points[vertpos + 2], &m_step_verts[2], &vert);
                    vec3_add(&vbdata->points[vertpos + 3], &m_step_verts[3], &vert);
                    vec3_add(&vbdata->points[vertpos + 4], &m_step_verts[4], &vert);
                    vec3_add(&vbdata->points[vertpos + 5], &m_step_verts[5], &vert);
                    vertpos += 6;
                }
            }
            else
            {
                auto x1 = (float)(i * bar_stride);
                auto x2 = x1 + m_bar_width;
                auto offset = (m_rounded_caps ? m_cap_radius : 0.0f) + channel_offset;
                if(channel)
                {
                    val = bottom - val;
                    offset = -offset;
                }
                auto bot = ((m_rounded_caps && !m_stereo) || (m_channel_spacing > 0)) ? (cpos - offset) : cpos;
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
                    auto half = m_cap_tris / 2; // m_cap_tris always even
                    auto start = m_radial ? 0 : (channel ? 0 : half);
                    auto stop = m_radial ? m_cap_tris : (start + half);
                    vec3 cvert;
                    vec3_set(&cvert, ccx, val, 0.0f);
                    for(auto j = start; j < stop; ++j)
                    {
                        vec3_add(&vbdata->points[vertpos], &m_cap_verts[j], &cvert);
                        vec3_add(&vbdata->points[vertpos + 1], &m_cap_verts[j + 1], &cvert);
                        vec3_copy(&vbdata->points[vertpos + 2], &cvert);
                        vertpos += 3;
                    }
                    if(!m_stereo || (m_channel_spacing > 0))
                    {
                        auto ccy = cpos - offset;
                        start = m_radial ? 0 : (channel ? half : 0);
                        stop = m_radial ? m_cap_tris : (start + half);
                        vec3_set(&cvert, ccx, ccy, 0.0f);
                        for(auto j = start; j < stop; ++j)
                        {
                            vec3_add(&vbdata->points[vertpos], &m_cap_verts[j], &cvert);
                            vec3_add(&vbdata->points[vertpos + 1], &m_cap_verts[j + 1], &cvert);
                            vec3_copy(&vbdata->points[vertpos + 2], &cvert);
                            vertpos += 3;
                        }
                    }
                }
            }
        }

        gs_vertexbuffer_flush(m_vbuf);

        if(vertpos > 0)
            gs_draw(GS_TRIS, 0, vertpos);
    }

    gs_load_vertexbuffer(nullptr);
    gs_technique_end_pass(tech);
    gs_technique_end(tech);
}

gs_technique_t *WAVSource::get_shader_tech()
{
    const char *techname;
    if(m_radial)
        techname = (m_render_mode == RenderMode::GRADIENT) ? "RadialGradient" : "Radial";
    else
        techname = (m_render_mode == RenderMode::GRADIENT) ? "Gradient" : "Solid";
    return gs_effect_get_technique(m_shader, techname);
}

void WAVSource::set_shader_vars(float cpos, float miny, float minpos, float channel_offset, float border_top, float border_bottom)
{
    if(m_render_mode == RenderMode::PULSE)
    {
        bool bars = (m_display_mode == DisplayMode::BAR) || (m_display_mode == DisplayMode::STEPPED_BAR) || m_meter_mode;
        vec4 color;
        auto range = border_bottom - border_top;
        auto t = (m_pulse_mode == PulseMode::MAGNITUDE) ? saturate((border_bottom - miny) / (range * m_grad_ratio)) : saturate(minpos / ((bars ? (float)(m_num_bars - 1) : (float)(m_width - 1)) * m_grad_ratio));
        auto x = lerp(m_color_base.x, m_color_crest.x, t);
        auto y = lerp(m_color_base.y, m_color_crest.y, t);
        auto z = lerp(m_color_base.z, m_color_crest.z, t);
        auto w = lerp(m_color_base.w, m_color_crest.w, t);
        vec4_set(&color, x, y, z, w);

        auto color_base = gs_effect_get_param_by_name(m_shader, "color_base");
        gs_effect_set_vec4(color_base, &color);
    }
    else
    {
        auto color_base = gs_effect_get_param_by_name(m_shader, "color_base");
        gs_effect_set_vec4(color_base, &m_color_base);

        if(m_render_mode == RenderMode::GRADIENT)
        {
            auto color_crest = gs_effect_get_param_by_name(m_shader, "color_crest");
            gs_effect_set_vec4(color_crest, &m_color_crest);
            auto grad_height = gs_effect_get_param_by_name(m_shader, "grad_height");
            gs_effect_set_float(grad_height, (cpos - miny - channel_offset) * m_grad_ratio);
            auto grad_center = gs_effect_get_param_by_name(m_shader, "grad_center");
            gs_effect_set_float(grad_center, cpos);
            auto grad_offset = gs_effect_get_param_by_name(m_shader, "grad_offset");
            gs_effect_set_float(grad_offset, channel_offset);
        }
    }

    if(m_radial)
    {
        auto graph_width = gs_effect_get_param_by_name(m_shader, "graph_width");
        gs_effect_set_float(graph_width, float(m_width - 1));
        auto graph_height = gs_effect_get_param_by_name(m_shader, "graph_height");
        gs_effect_set_float(graph_height, (float)m_height);
        auto graph_deadzone = gs_effect_get_param_by_name(m_shader, "graph_deadzone");
        gs_effect_set_float(graph_deadzone, m_deadzone);
        auto radial_arc = gs_effect_get_param_by_name(m_shader, "radial_arc");
        gs_effect_set_float(radial_arc, m_radial_arc);
        auto radial_rotation = gs_effect_get_param_by_name(m_shader, "radial_rotation");
        gs_effect_set_float(radial_rotation, m_radial_rotation);
        auto graph_invert = gs_effect_get_param_by_name(m_shader, "graph_invert");
        gs_effect_set_bool(graph_invert, m_invert);
        auto radial_center = gs_effect_get_param_by_name(m_shader, "radial_center");
        vec2 rc;
        vec2_set(&rc, (float)m_height + m_deadzone, (float)m_height + m_deadzone);
        gs_effect_set_vec2(radial_center, &rc);
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
#ifndef DISABLE_X86_SIMD
    if(HAVE_AVX2)
        arch += " AVX2";
    if(HAVE_AVX)
        arch += " AVX";
    if(HAVE_FMA3)
        arch += " FMA3";
    arch += " SSE2";
#else
    arch = " Generic";
#endif // !DISABLE_X86_SIMD

#if defined(__x86_64__) || defined(_M_X64)
    LogInfo << "Registered v" VERSION_STRING " x64";
#elif defined(__i386__) || defined(_M_IX86)
    LogInfo << "Registered v" VERSION_STRING " x86";
#elif defined(__aarch64__) || defined(_M_ARM64)
    LogInfo << "Registered v" VERSION_STRING " ARM64";
#elif defined(__arm__) || defined(_M_ARM)
    LogInfo << "Registered v" VERSION_STRING " ARM";
#else
    LogInfo << "Registered v" VERSION_STRING " Unknown Arch";
#endif
    LogInfo << "Using CPU capabilities:" << arch;

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
    if(audio == nullptr)
        return;
    if(!m_mtx.try_lock_for(std::chrono::milliseconds(10)))
        return;
    std::lock_guard lock(m_mtx, std::adopt_lock);
    if(((m_audio_source == nullptr) && !m_output_bus_captured) || (m_capture_channels == 0))
        return;
    assert((m_channel_base >= 0) && (m_channel_base < (int)get_audio_channels(m_audio_info.speakers)));
    assert((m_channel_base == 0) || (m_capture_channels == 1));

    // audio sync
    m_capture_ts = os_gettime_ns();
    auto audio_len = audio_frames_to_ns(m_audio_info.samples_per_sec, audio->frames);
    auto delta = std::max(audio->timestamp, m_capture_ts) - std::min(audio->timestamp, m_capture_ts);
    if(delta > MAX_TS_DELTA) // attempt to handle extreme / bogus timestamps (e.g. VLC)
        m_audio_ts = m_capture_ts;
    else
        m_audio_ts = audio->timestamp + audio_len;
    const auto bufsz = m_fft_size * sizeof(float);
    const int64_t dtaudio = get_audio_sync(m_capture_ts);
    const size_t dtsamples = (dtaudio > 0) ? (size_t)ns_to_audio_frames(m_audio_info.samples_per_sec, (uint64_t)dtaudio) : 0;

    // RMS
    if(m_normalize_volume)
    {
        auto frames = (size_t)audio->frames;
        auto data = (float**)&audio->data;
        while(frames > 0)
        {
            auto count = std::min(frames, (size_t)AUDIO_OUTPUT_FRAMES);
            for(size_t i = 0u; i < count; ++i)
            {
                // sum only the largest sample of all channels from each time point
                // this prevents excessive boosting when one channel is quiet (and reduces the amount of buffering required)
                float val = 0.0f;
                for(auto channel = 0u; channel < m_capture_channels; ++channel)
                {
                    auto buf = data[m_channel_base + channel];
                    if(buf != nullptr)
                        val = std::max(std::abs(buf[i]), val);
                }
                m_rms_temp_buf[i] = val * val;
            }
            circlebuf_push_back(&m_rms_sync_buf, m_rms_temp_buf.get(), count * sizeof(float));
            frames -= count;
        }

        const size_t max_rms_size = (dtsamples * sizeof(float)) + (m_input_rms_size * sizeof(float));
        auto total = m_rms_sync_buf.size;
        if(total > max_rms_size)
            circlebuf_pop_front(&m_rms_sync_buf, nullptr, total - max_rms_size);
    }

    auto sz = size_t(audio->frames * sizeof(float));
    for(auto i = m_channel_base; i < (m_channel_base + (int)m_capture_channels); ++i)
    {
        auto j = i - m_channel_base;
        assert((j == 0) || (j == 1));
        if((muted && !m_ignore_mute) || (audio->data[i] == nullptr))
            circlebuf_push_back_zero(&m_capturebufs[j], sz);
        else
            circlebuf_push_back(&m_capturebufs[j], audio->data[i], sz);

        const size_t max_size = (dtsamples * sizeof(float)) + bufsz;
        auto total = m_capturebufs[j].size;
        if(total > max_size)
            circlebuf_pop_front(&m_capturebufs[j], nullptr, total - max_size);
    }
}

void WAVSource::capture_output_bus([[maybe_unused]] size_t mix_idx, const audio_data *audio)
{
    capture_audio(nullptr, audio, false);
}
