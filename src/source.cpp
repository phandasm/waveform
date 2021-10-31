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

#include "math_funcs.hpp"
#include "source.hpp"
#include "settings.hpp"
#include <immintrin.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cstring>
#include <limits>
#include "cpuinfo_x86.h"

static const float LOG_MIN = std::log10(std::numeric_limits<float>::min());
static const float DB_MIN = 20.0f * LOG_MIN;

static const auto CPU_INFO = cpu_features::GetX86Info();
static const bool HAVE_AVX2 = CPU_INFO.features.avx2 && CPU_INFO.features.fma3;
static const bool HAVE_AVX = CPU_INFO.features.avx && CPU_INFO.features.fma3;

static inline float dbfs(float mag)
{
    if(mag > 0.0f)
        return 20.0f * std::log10(mag);
    else
        return DB_MIN;
}

static bool enum_callback(void *data, obs_source_t *src)
{
    if(obs_source_get_output_flags(src) & OBS_SOURCE_AUDIO) // filter sources without audio
        reinterpret_cast<std::vector<std::string>*>(data)->push_back(obs_source_get_name(src));
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
        info->samples_per_sec = 44100;
        info->speakers = SPEAKERS_UNKNOWN;
    }
}

// Callbacks for obs_source_info structure
namespace callbacks {
    static const char *get_name([[maybe_unused]] void *data)
    {
        return T("source_name");
    }

    static void *create(obs_data_t *settings, obs_source_t *source)
    {
        if(HAVE_AVX2)
            return reinterpret_cast<void*>(new WAVSourceAVX2(settings, source));
        else if(HAVE_AVX)
            return reinterpret_cast<void*>(new WAVSourceAVX(settings, source));
        else
            return reinterpret_cast<void*>(new WAVSourceSSE2(settings, source));
    }

    static void destroy(void *data)
    {
        delete reinterpret_cast<WAVSource*>(data);
    }

    static uint32_t get_width(void *data)
    {
        return reinterpret_cast<WAVSource*>(data)->width();
    }

    static uint32_t get_height(void *data)
    {
        return reinterpret_cast<WAVSource*>(data)->height();
    }

    static void get_defaults(obs_data_t *settings)
    {
        obs_data_set_default_string(settings, P_AUDIO_SRC, P_NONE);
        obs_data_set_default_int(settings, P_WIDTH, 800);
        obs_data_set_default_int(settings, P_HEIGHT, 600);
        obs_data_set_default_string(settings, P_CHANNEL_MODE, P_STEREO);
        obs_data_set_default_int(settings, P_FFT_SIZE, 1024);
        obs_data_set_default_bool(settings, P_AUTO_FFT_SIZE, false);
        obs_data_set_default_string(settings, P_WINDOW, P_HANN);
        obs_data_set_default_string(settings, P_INTERP_MODE, P_LANCZOS);
        obs_data_set_default_string(settings, P_TSMOOTHING, P_EXPAVG);
        obs_data_set_default_double(settings, P_GRAVITY, 0.65);
        obs_data_set_default_bool(settings, P_FAST_PEAKS, false);
        obs_data_set_default_int(settings, P_CUTOFF_LOW, 120);
        obs_data_set_default_int(settings, P_CUTOFF_HIGH, 17500);
        obs_data_set_default_int(settings, P_FLOOR, -95);
        obs_data_set_default_int(settings, P_CEILING, 0);
        obs_data_set_default_double(settings, P_SLOPE, 0.0);
        obs_data_set_default_string(settings, P_RENDER_MODE, P_SOLID);
        obs_data_set_default_int(settings, P_COLOR_BASE, 0xffffffff);
        obs_data_set_default_int(settings, P_COLOR_CREST, 0xffffffff);
        obs_data_set_default_double(settings, P_GRAD_RATIO, 1.35);
    }

    static obs_properties_t *get_properties([[maybe_unused]] void *data)
    {
        auto props = obs_properties_create();

        // audio source
        auto srclist = obs_properties_add_list(props, P_AUDIO_SRC, T(P_AUDIO_SRC), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
        obs_property_list_add_string(srclist, T(P_NONE), P_NONE);

        for(const auto& str : enumerate_audio_sources())
            obs_property_list_add_string(srclist, str.c_str(), str.c_str());

        // video size
        obs_properties_add_int(props, P_WIDTH, T(P_WIDTH), 32, 3840, 1);
        obs_properties_add_int(props, P_HEIGHT, T(P_HEIGHT), 32, 2160, 1);

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
                obs_property_set_enabled(obs_properties_get(props, P_FFT_SIZE), !obs_data_get_bool(settings, P_AUTO_FFT_SIZE));
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
            obs_property_set_enabled(obs_properties_get(props, P_GRAVITY), enable);
            obs_property_set_enabled(obs_properties_get(props, P_FAST_PEAKS), enable);
            obs_property_set_visible(obs_properties_get(props, P_GRAVITY), enable);
            obs_property_set_visible(obs_properties_get(props, P_FAST_PEAKS), enable);
            return true;
            });

        // interpolation
        auto interplist = obs_properties_add_list(props, P_INTERP_MODE, T(P_INTERP_MODE), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
        obs_property_list_add_string(interplist, T(P_POINT), P_POINT);
        obs_property_list_add_string(interplist, T(P_LANCZOS), P_LANCZOS);
        obs_property_set_long_description(interplist, T(P_INTERP_DESC));

        // display
        auto low_cut = obs_properties_add_int_slider(props, P_CUTOFF_LOW, T(P_CUTOFF_LOW), 0, 24000, 1);
        auto high_cut = obs_properties_add_int_slider(props, P_CUTOFF_HIGH, T(P_CUTOFF_HIGH), 0, 24000, 1);
        obs_property_int_set_suffix(low_cut, " Hz");
        obs_property_int_set_suffix(high_cut, " Hz");
        auto floor = obs_properties_add_int_slider(props, P_FLOOR, T(P_FLOOR), -240, 0, 1);
        auto ceiling = obs_properties_add_int_slider(props, P_CEILING, T(P_CEILING), -240, 0, 1);
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
            obs_property_set_enabled(obs_properties_get(props, P_GRAD_RATIO), enable);
            obs_property_set_visible(obs_properties_get(props, P_GRAD_RATIO), enable);
            return true;
            });

        return props;
    }

    static void update(void *data, obs_data_t *settings)
    {
        reinterpret_cast<WAVSource*>(data)->update(settings);
    }

    static void show(void *data)
    {
        reinterpret_cast<WAVSource*>(data)->show();
    }

    static void hide(void *data)
    {
        reinterpret_cast<WAVSource*>(data)->hide();
    }

    static void tick(void *data, float seconds)
    {
        reinterpret_cast<WAVSource*>(data)->tick(seconds);
    }

    static void render(void *data, gs_effect_t *effect)
    {
        reinterpret_cast<WAVSource*>(data)->render(effect);
    }

    static void capture_audio(void *data, obs_source_t *source, const audio_data *audio, bool muted)
    {
        reinterpret_cast<WAVSource*>(data)->capture_audio(source, audio, muted);
    }
}

void WAVSource::get_settings(obs_data_t *settings)
{
    m_width = (unsigned int)obs_data_get_int(settings, P_WIDTH);
    m_height = (unsigned int)obs_data_get_int(settings, P_HEIGHT);
    m_stereo = p_equ(obs_data_get_string(settings, P_CHANNEL_MODE), P_STEREO);
    m_fft_size = (size_t)obs_data_get_int(settings, P_FFT_SIZE);
    m_auto_fft_size = obs_data_get_bool(settings, P_AUTO_FFT_SIZE);
    auto wnd = obs_data_get_string(settings, P_WINDOW);
    auto tsmoothing = obs_data_get_string(settings, P_TSMOOTHING);
    m_gravity = (float)obs_data_get_double(settings, P_GRAVITY);
    m_fast_peaks = obs_data_get_bool(settings, P_FAST_PEAKS);
    auto interp = obs_data_get_string(settings, P_INTERP_MODE);
    m_cutoff_low = (int)obs_data_get_int(settings, P_CUTOFF_LOW);
    m_cutoff_high = (int)obs_data_get_int(settings, P_CUTOFF_HIGH);
    m_floor = (int)obs_data_get_int(settings, P_FLOOR);
    m_ceiling = (int)obs_data_get_int(settings, P_CEILING);
    m_slope = (float)obs_data_get_double(settings, P_SLOPE);
    auto rendermode = obs_data_get_string(settings, P_RENDER_MODE);
    auto color_base = obs_data_get_int(settings, P_COLOR_BASE);
    auto color_crest = obs_data_get_int(settings, P_COLOR_CREST);
    m_grad_ratio = (float)obs_data_get_double(settings, P_GRAD_RATIO);

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
}

void WAVSource::recapture_audio(obs_data_t *settings)
{
    // release old capture
    release_audio_capture();

    // add new capture
    auto src_name = obs_data_get_string(settings, P_AUDIO_SRC);
    if(src_name != nullptr)
    {
        auto asrc = obs_get_source_by_name(src_name);
        if(asrc != nullptr)
        {
            obs_source_add_audio_capture_callback(asrc, &callbacks::capture_audio, this);
            m_audio_source = obs_source_get_weak_source(asrc);
            obs_source_release(asrc);
        }
    }
}

void WAVSource::release_audio_capture()
{
    if(m_audio_source != nullptr)
    {
        auto src = obs_weak_source_get_source(m_audio_source);
        m_audio_source = nullptr;
        if(src != nullptr)
        {
            obs_source_remove_audio_capture_callback(src, &callbacks::capture_audio, this);
            obs_source_release(src);
        }
    }

    // reset circular buffers
    for(auto& i : m_capturebufs)
    {
        i.end_pos = 0;
        i.start_pos = 0;
        i.size = 0;
    }
}

void WAVSource::free_fft()
{
    for(auto i = 0; i < 2; ++i)
    {
        m_decibels[i].reset();
        m_tsmooth_buf[i].reset();
    }

    m_fft_input.reset();
    m_fft_output.reset();
    m_window_coefficients.reset();

    if(m_fft_plan != nullptr)
    {
        fftwf_destroy_plan(m_fft_plan);
        m_fft_plan = nullptr;
    }

    m_fft_size = 0;
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
    free_fft();

    for(auto& i : m_capturebufs)
        circlebuf_free(&i);
}

void WAVSource::update(obs_data_t *settings)
{
    std::lock_guard lock(m_mtx);

    release_audio_capture();
    free_fft();
    get_settings(settings);

    // get current audio settings
    update_audio_info(&m_audio_info);
    m_capture_channels = get_audio_channels(m_audio_info.speakers);

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
        auto count = m_fft_size / 2;
        m_decibels[i].reset(avx_alloc<float>(count));
        if(m_tsmoothing != TSmoothingMode::NONE)
            m_tsmooth_buf[i].reset(avx_alloc<float>(count));
        for(auto j = 0; j < count; ++j)
        {
            m_decibels[i][j] = DB_MIN;
            if(m_tsmoothing != TSmoothingMode::NONE)
                m_tsmooth_buf[i][j] = 0;
        }
    }
    m_fft_input.reset(avx_alloc<float>(m_fft_size));
    m_fft_output.reset(avx_alloc<fftwf_complex>(m_fft_size));
    m_fft_plan = fftwf_plan_dft_r2c_1d((int)m_fft_size, m_fft_input.get(), m_fft_output.get(), FFTW_ESTIMATE);

    // window function
    if(m_window_func != FFTWindow::NONE)
    {
        // precompute window coefficients
        m_window_coefficients.reset(avx_alloc<float>(m_fft_size));
        const auto N = m_fft_size - 1;
        constexpr auto pi2 = 2 * (float)M_PI;
        constexpr auto pi4 = 4 * (float)M_PI;
        constexpr auto pi6 = 6 * (float)M_PI;
        if(m_window_func == FFTWindow::HANN)
        {
            for(size_t i = 0; i < m_fft_size; ++i)
                m_window_coefficients[i] = 0.5f * (1 - std::cos((pi2 * i) / N));
        }
        else if(m_window_func == FFTWindow::HAMMING)
        {
            for(size_t i = 0; i < m_fft_size; ++i)
                m_window_coefficients[i] = 0.53836f - (0.46164f * std::cos((pi2 * i) / N));
        }
        else if(m_window_func == FFTWindow::BLACKMAN)
        {
            for(size_t i = 0; i < m_fft_size; ++i)
                m_window_coefficients[i] = 0.42f - (0.5f * std::cos((pi2 * i) / N)) + (0.08f * std::cos((pi4 * i) / N));
        }
        else if(m_window_func == FFTWindow::BLACKMAN_HARRIS)
        {
            for(size_t i = 0; i < m_fft_size; ++i)
                m_window_coefficients[i] = 0.35875f - (0.48829f * std::cos((pi2 * i) / N)) + (0.14128f * std::cos((pi4 * i) / N)) - (0.01168f * std::cos((pi6 * i) / N));
        }
    }

    m_last_silent = false;
    m_render_silent = false;

    recapture_audio(settings);
    for(auto& i : m_capturebufs)
    {
        auto bufsz = m_fft_size * sizeof(float);
        if(i.size < bufsz)
            circlebuf_push_back_zero(&i, bufsz - i.size);
    }
}

// TODO: optimize this mess
void WAVSource::render([[maybe_unused]] gs_effect_t *effect)
{
    std::lock_guard lock(m_mtx);
    if(m_render_silent && m_last_silent) // account for possible dropped frames
        return;
    m_render_silent = m_last_silent;

    const auto num_verts = (m_render_mode == RenderMode::LINE) ? m_width : (m_width + 2);
    auto vbdata = gs_vbdata_create();
    vbdata->num = num_verts;
    vbdata->points = (vec3*)bmalloc(num_verts * sizeof(vec3));
    gs_vertbuffer_t *vbuf = nullptr;

    auto filename = obs_module_file("gradient.effect");
    auto shader = gs_effect_create_from_file(filename, nullptr);
    bfree(filename);
    auto color_base = gs_effect_get_param_by_name(shader, "color_base");
    auto tech = gs_effect_get_technique(shader, (m_render_mode == RenderMode::GRADIENT) ? "Gradient" : "Solid");

    const auto maxbin = (m_fft_size / 2) - 1;
    const auto sr = (float)m_audio_info.samples_per_sec;
    const auto lowbin = std::clamp((float)m_cutoff_low * m_fft_size / sr, 1.0f, (float)maxbin);
    const auto highbin = std::clamp((float)m_cutoff_high * m_fft_size / sr, 1.0f, (float)maxbin);
    const auto center = (float)m_height / 2 + 0.5f;
    const auto right = (float)m_width + 0.5f;
    const auto bottom = (float)m_height + 0.5f;
    const auto dbrange = m_ceiling - m_floor;

    if(m_render_mode == RenderMode::GRADIENT)
    {
        // find highest fft bin and calculate it's y coord
        // used to scale the gradient
        auto miny = DB_MIN;
        auto color_dist = gs_effect_get_param_by_name(shader, "distfactor");
        for(auto channel = 0u; channel < (m_stereo ? 2u : 1u); ++channel)
            for(auto i = 1; i < m_fft_size / 2; ++i)
                if(m_decibels[channel][i] > miny)
                    miny = m_decibels[channel][i];
        miny = lerp(0.5f, m_stereo ? center : bottom, std::clamp(m_ceiling - miny, 0.0f, (float)dbrange) / dbrange);
        gs_effect_set_float(color_dist, 1 / ((m_stereo ? center : bottom) - miny) * m_grad_ratio);

        auto color_crest = gs_effect_get_param_by_name(shader, "color_crest");
        auto grad_center_pos = gs_effect_get_param_by_name(shader, "center");
        vec2 centervec{ m_width / 2 + 0.5f, (m_stereo ? center : bottom) };
        gs_effect_set_vec4(color_crest, &m_color_crest);
        gs_effect_set_vec2(grad_center_pos, &centervec);
    }

    gs_effect_set_vec4(color_base, &m_color_base);

    gs_technique_begin(tech);
    gs_technique_begin_pass(tech, 0);

    for(auto channel = 0u; channel < (m_stereo ? 2u : 1u); ++channel)
    {
        auto vertpos = 0u;
        if(channel)
            vbdata = gs_vertexbuffer_get_data(vbuf);
        if(m_render_mode != RenderMode::LINE)
            vec3_set(&vbdata->points[vertpos++], -0.5, m_stereo ? center : bottom, 0);

        for(auto i = 0u; i < m_width; ++i)
        {
            if((m_render_mode != RenderMode::LINE) && (i & 1))
            {
                vec3_set(&vbdata->points[vertpos++], (float)i + 0.5f, m_stereo ? center : bottom, 0);
                continue;
            }

            auto bin = log_interp(lowbin, highbin, (float)i / (float)(m_width - 1));
            float val;
            if(m_interp_mode == InterpMode::LANCZOS)
                val = lanczos_interp(bin, 3.0f, m_fft_size / 2, m_decibels[channel].get());
            else
                val = m_decibels[channel][(int)bin];
            val = lerp(0.5f, m_stereo ? center : bottom, std::clamp(m_ceiling - val, 0.0f, (float)dbrange) / dbrange);
            if(channel == 0)
                vec3_set(&vbdata->points[vertpos++], (float)i + 0.5f, val, 0);
            else
                vec3_set(&vbdata->points[vertpos++], (float)i + 0.5f, bottom - val, 0);
        }

        if(m_render_mode != RenderMode::LINE)
            vec3_set(&vbdata->points[vertpos++], right, m_stereo ? center : bottom, 0);

        if(channel)
            gs_vertexbuffer_flush(vbuf);
        else
            vbuf = gs_vertexbuffer_create(vbdata, GS_DYNAMIC);
        gs_load_vertexbuffer(vbuf);
        gs_load_indexbuffer(nullptr);
        gs_draw((m_render_mode != RenderMode::LINE) ? GS_TRISTRIP : GS_LINESTRIP, 0, num_verts);
    }

    gs_vertexbuffer_destroy(vbuf);
    gs_technique_end_pass(tech);
    gs_technique_end(tech);

    gs_effect_destroy(shader);
}

void WAVSource::register_source()
{
    auto arch = "AVX2, FMA3";
    if(!HAVE_AVX2)
        arch = HAVE_AVX ? "AVX, FMA3" : "SSE2";
#if defined(__x86_64__) || defined(_M_X64)
    blog(LOG_INFO, "[" MODULE_NAME "]: Registered v%s 64-bit", VERSION_STRING);
#elif defined(__i386__) || defined(_M_IX86)
    blog(LOG_INFO, "[" MODULE_NAME "]: Registered v%s 32-bit", VERSION_STRING);
#else
    blog(LOG_INFO, "[" MODULE_NAME "]: Registered v%s Unknown Arch", VERSION_STRING);
#endif
    blog(LOG_INFO, "[" MODULE_NAME "]: Using CPU capabilities: %s", arch);

    obs_source_info info{};
    info.id = MODULE_NAME "_source";
    info.type = OBS_SOURCE_TYPE_INPUT;
    info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_DO_NOT_DUPLICATE;
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
    if(!m_mtx.try_lock_for(std::chrono::milliseconds(3)))
        return;
    std::lock_guard lock(m_mtx, std::adopt_lock);
    if(m_audio_source == nullptr)
        return;

    auto sz = size_t(audio->frames * sizeof(float));
    for(auto i = 0u; i < std::min(m_capture_channels, 2u); ++i)
    {
        if(muted)
            circlebuf_push_back_zero(&m_capturebufs[i], sz);
        else
            circlebuf_push_back(&m_capturebufs[i], audio->data[i], sz);

        auto total = m_capturebufs[i].size;
        auto max = m_fft_size * sizeof(float) * 2;
        if(total > max)
            circlebuf_pop_front(&m_capturebufs[i], nullptr, total - max);
    }
}

void WAVSourceAVX2::tick([[maybe_unused]] float seconds)
{
    std::lock_guard lock(m_mtx);
    if(m_capture_channels == 0)
        return;

    const auto bufsz = m_fft_size * sizeof(float);
    const auto outsz = m_fft_size / 2; // discard bins at nyquist and above
    constexpr auto step = sizeof(__m256) / sizeof(float);

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

    for(auto channel = 0u; channel < m_capture_channels; ++channel)
    {
        // get captured audio
        if(m_capturebufs[channel].size >= bufsz)
        {
            circlebuf_peek_front(&m_capturebufs[channel], m_fft_input.get(), bufsz);
            circlebuf_pop_front(&m_capturebufs[channel], nullptr, m_capturebufs[channel].size - bufsz);
        }
        else
            continue;

        // skip FFT for silent audio
        bool silent = true;
        auto zero = _mm256_set1_ps(0.0);
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
                return;
            bool outsilent = true;
            auto floor = _mm256_set1_ps((float)m_floor - 10);
            for(auto ch = 0; ch < (m_stereo ? 2 : 1); ++ch)
            {
                for(size_t i = 0; i < outsz; i += step)
                {
                    auto mask = _mm256_cmp_ps(floor, _mm256_load_ps(&m_decibels[ch][i]), _CMP_GT_OQ);
                    if(_mm256_movemask_ps(mask) != 0xff)
                    {
                        outsilent = false;
                        break;
                    }
                }
                if(!outsilent)
                    break;
            }
            if(outsilent)
            {
                m_last_silent = true;
                return;
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
        //const auto abs_mask = _mm256_set1_ps(-0.0);
        const auto mag_coefficient = _mm256_div_ps(_mm256_set1_ps(2.0f), _mm256_set1_ps((float)m_fft_size));
        for(size_t i = 0; i < outsz; i += step)
        {
            // this *should* be faster than 2x vgatherxxx instructions
            // load 8 real/imaginary pairs and group the r/i components in the low/high halves
            const auto buf = (float*)&m_fft_output[i];
            auto chunk1 = _mm256_permutevar8x32_ps(_mm256_load_ps(buf), shuffle_mask);
            auto chunk2 = _mm256_permutevar8x32_ps(_mm256_load_ps(&buf[step]), shuffle_mask);

            // pack the real and imaginary components into separate vectors
            auto rvec = _mm256_permute2f128_ps(chunk1, chunk2, 0 | (2 << 4));
            auto ivec = _mm256_permute2f128_ps(chunk1, chunk2, 1 | (3 << 4));

            // absoulte value (set sign bit = 0)
            //rvec = _mm256_andnot_ps(abs_mask, rvec);
            //ivec = _mm256_andnot_ps(abs_mask, ivec);

            // calculate normalized magnitude
            // 2 * magnitude / N
            auto mag = _mm256_sqrt_ps(_mm256_fmadd_ps(ivec, ivec, _mm256_mul_ps(rvec, rvec))); // magnitude sqrt(r^2 + i^2)
            mag = _mm256_mul_ps(mag, mag_coefficient); // 2 * magnitude / N with precomputed quotient

            // time domain smoothing
            if(m_tsmoothing == TSmoothingMode::EXPONENTIAL)
            {
                // take new values immediately if larger
                if(m_fast_peaks)
                {
                    auto mask = _mm256_cmp_ps(mag, _mm256_load_ps(&m_tsmooth_buf[channel][i]), _CMP_GT_OQ);
                    _mm256_maskstore_ps(&m_tsmooth_buf[channel][i], _mm256_castps_si256(mask), mag);
                }
                auto g = _mm256_set1_ps(m_gravity);
                auto g2 = _mm256_sub_ps(_mm256_set1_ps(1.0), g); // 1 - gravity

                // (gravity * oldval) + ((1 - gravity) * newval)
                mag = _mm256_fmadd_ps(g, _mm256_load_ps(&m_tsmooth_buf[channel][i]), _mm256_mul_ps(g2, mag));
                _mm256_store_ps(&m_tsmooth_buf[channel][i], mag);
            }

            _mm256_store_ps(&m_decibels[channel][i], mag); // end of the line for AVX
        }
    }

    if(m_output_channels > m_capture_channels)
        memcpy(m_decibels[1].get(), m_decibels[0].get(), outsz * sizeof(float));

    // dBFS conversion
    // 20 * log(2 * magnitude / N)
    const bool slope = m_slope != 0.0f;
    if(m_stereo)
    {
        for(auto channel = 0; channel < 2; ++channel)
            for(size_t i = 0; i < outsz; ++i)
                if(slope)
                    m_decibels[channel][i] = std::clamp(m_slope * std::log10(i ? (float)i : 1.0f) + dbfs(m_decibels[channel][i]), DB_MIN, 0.0f);
                else
                    m_decibels[channel][i] = dbfs(m_decibels[channel][i]);
    }
    else if(m_capture_channels > 1)
    {
        for(size_t i = 0; i < outsz; ++i)
            if(slope)
                m_decibels[0][i] = std::clamp(m_slope * std::log10(i ? (float)i : 1.0f) + dbfs((m_decibels[0][i] + m_decibels[1][i]) / 2), DB_MIN, 0.0f);
            else
                m_decibels[0][i] = dbfs((m_decibels[0][i] + m_decibels[1][i]) / 2);
    }
    else
    {
        for(size_t i = 0; i < outsz; ++i)
            if(slope)
                m_decibels[0][i] = std::clamp(m_slope * std::log10(i ? (float)i : 1.0f) + dbfs(m_decibels[0][i]), DB_MIN, 0.0f);
            else
                m_decibels[0][i] = dbfs(m_decibels[0][i]);
    }
}

// adaptation of WAVSourceAVX2 to support CPUs without AVX2
// see comments of WAVSourceAVX2
void WAVSourceAVX::tick([[maybe_unused]] float seconds)
{
    std::lock_guard lock(m_mtx);
    if(m_capture_channels == 0)
        return;

    const auto bufsz = m_fft_size * sizeof(float);
    const auto outsz = m_fft_size / 2;
    constexpr auto step = sizeof(__m256) / sizeof(float);

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
        auto zero = _mm256_set1_ps(0.0);
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

        if(silent)
        {
            if(m_last_silent)
                return;
            bool outsilent = true;
            auto floor = _mm256_set1_ps((float)m_floor - 10);
            for(auto ch = 0; ch < (m_stereo ? 2 : 1); ++ch)
            {
                for(size_t i = 0; i < outsz; i += step)
                {
                    auto mask = _mm256_cmp_ps(floor, _mm256_load_ps(&m_decibels[ch][i]), _CMP_GT_OQ);
                    if(_mm256_movemask_ps(mask) != 0xff)
                    {
                        outsilent = false;
                        break;
                    }
                }
                if(!outsilent)
                    break;
            }
            if(outsilent)
            {
                m_last_silent = true;
                return;
            }
        }

        if(m_window_func != FFTWindow::NONE)
        {
            auto inbuf = m_fft_input.get();
            auto mulbuf = m_window_coefficients.get();
            for(auto i = 0u; i < m_fft_size; i += step)
                _mm256_store_ps(&inbuf[i], _mm256_mul_ps(_mm256_load_ps(&inbuf[i]), _mm256_load_ps(&mulbuf[i])));
        }

        if(m_fft_plan != nullptr)
            fftwf_execute(m_fft_plan);
        else
            continue;

        constexpr auto shuffle_mask_r = 0 | (2 << 2) | (0 << 4) | (2 << 6);
        constexpr auto shuffle_mask_i = 1 | (3 << 2) | (1 << 4) | (3 << 6);
        const auto mag_coefficient = _mm256_div_ps(_mm256_set1_ps(2.0f), _mm256_set1_ps((float)m_fft_size));
        for(size_t i = 0; i < outsz; i += step)
        {
            // load 8 real/imaginary pairs and group the r/i components in the low/high halves
            // de-interleaving 256-bit float vectors is nigh impossible without AVX2, so we'll
            // use 128-bit vectors and merge them, but i question if this is better than a 128-bit loop
            const auto buf = (float*)&m_fft_output[i];
            auto chunk1 = _mm_load_ps(buf);
            auto chunk2 = _mm_load_ps(&buf[4]);
            auto rvec = _mm256_castps128_ps256(_mm_shuffle_ps(chunk1, chunk2, shuffle_mask_r)); // group octwords
            auto ivec = _mm256_castps128_ps256(_mm_shuffle_ps(chunk1, chunk2, shuffle_mask_i));
            chunk1 = _mm_load_ps(&buf[8]);
            chunk2 = _mm_load_ps(&buf[12]);
            rvec = _mm256_insertf128_ps(rvec, _mm_shuffle_ps(chunk1, chunk2, shuffle_mask_r), 1); // pack r/i octwords into separate 256-bit vecs
            ivec = _mm256_insertf128_ps(ivec, _mm_shuffle_ps(chunk1, chunk2, shuffle_mask_i), 1);

            auto mag = _mm256_sqrt_ps(_mm256_fmadd_ps(ivec, ivec, _mm256_mul_ps(rvec, rvec)));
            mag = _mm256_mul_ps(mag, mag_coefficient);

            if(m_tsmoothing == TSmoothingMode::EXPONENTIAL)
            {
                if(m_fast_peaks)
                {
                    auto mask = _mm256_cmp_ps(mag, _mm256_load_ps(&m_tsmooth_buf[channel][i]), _CMP_GT_OQ);
                    _mm256_maskstore_ps(&m_tsmooth_buf[channel][i], _mm256_castps_si256(mask), mag);
                }
                auto g = _mm256_set1_ps(m_gravity);
                auto g2 = _mm256_sub_ps(_mm256_set1_ps(1.0), g);

                mag = _mm256_fmadd_ps(g, _mm256_load_ps(&m_tsmooth_buf[channel][i]), _mm256_mul_ps(g2, mag));
                _mm256_store_ps(&m_tsmooth_buf[channel][i], mag);
            }

            _mm256_store_ps(&m_decibels[channel][i], mag);
        }
    }

    if(m_output_channels > m_capture_channels)
        memcpy(m_decibels[1].get(), m_decibels[0].get(), outsz * sizeof(float));

    const bool slope = m_slope != 0.0f;
    if(m_stereo)
    {
        for(auto channel = 0; channel < 2; ++channel)
            for(size_t i = 0; i < outsz; ++i)
                if(slope)
                    m_decibels[channel][i] = std::clamp(m_slope * std::log10(i ? (float)i : 1.0f) + dbfs(m_decibels[channel][i]), DB_MIN, 0.0f);
                else
                    m_decibels[channel][i] = dbfs(m_decibels[channel][i]);
    }
    else if(m_capture_channels > 1)
    {
        for(size_t i = 0; i < outsz; ++i)
            if(slope)
                m_decibels[0][i] = std::clamp(m_slope * std::log10(i ? (float)i : 1.0f) + dbfs((m_decibels[0][i] + m_decibels[1][i]) / 2), DB_MIN, 0.0f);
            else
                m_decibels[0][i] = dbfs((m_decibels[0][i] + m_decibels[1][i]) / 2);
    }
    else
    {
        for(size_t i = 0; i < outsz; ++i)
            if(slope)
                m_decibels[0][i] = std::clamp(m_slope * std::log10(i ? (float)i : 1.0f) + dbfs(m_decibels[0][i]), DB_MIN, 0.0f);
            else
                m_decibels[0][i] = dbfs(m_decibels[0][i]);
    }
}

// compatibility fallback using at most SSE2 instructions
void WAVSourceSSE2::tick([[maybe_unused]] float seconds)
{
    std::lock_guard lock(m_mtx);
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
        auto zero = _mm_set1_ps(0.0);
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
                return;
            bool outsilent = true;
            auto floor = _mm_set1_ps((float)m_floor - 10);
            for(auto ch = 0; ch < (m_stereo ? 2 : 1); ++ch)
            {
                for(size_t i = 0; i < outsz; i += step)
                {
                    auto mask = _mm_cmpgt_ps(floor, _mm_load_ps(&m_decibels[ch][i]));
                    if(_mm_movemask_ps(mask) != 0xf)
                    {
                        outsilent = false;
                        break;
                    }
                }
                if(!outsilent)
                    break;
            }
            if(outsilent)
            {
                m_last_silent = true;
                return;
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
        for(size_t i = 0; i < outsz; i += step)
        {
            // load 4 real/imaginary pairs and pack the r/i components into separate vectors
            const auto buf = (float*)&m_fft_output[i];
            auto chunk1 = _mm_load_ps(buf);
            auto chunk2 = _mm_load_ps(&buf[4]);
            auto rvec = _mm_shuffle_ps(chunk1, chunk2, shuffle_mask_r);
            auto ivec = _mm_shuffle_ps(chunk1, chunk2, shuffle_mask_i);

            auto mag = _mm_sqrt_ps(_mm_add_ps(_mm_mul_ps(ivec, ivec), _mm_mul_ps(rvec, rvec)));
            mag = _mm_mul_ps(mag, mag_coefficient);

            if(m_tsmoothing == TSmoothingMode::EXPONENTIAL)
            {
                if(m_fast_peaks)
                {
                    auto mask = _mm_cmpgt_ps(mag, _mm_load_ps(&m_tsmooth_buf[channel][i]));
                    _mm_maskmoveu_si128(_mm_castps_si128(mag), _mm_castps_si128(mask), (char*)&m_tsmooth_buf[channel][i]);
                }
                auto g = _mm_set1_ps(m_gravity);
                auto g2 = _mm_sub_ps(_mm_set1_ps(1.0), g);

                mag = _mm_add_ps(_mm_mul_ps(g, _mm_load_ps(&m_tsmooth_buf[channel][i])), _mm_mul_ps(g2, mag));
                _mm_store_ps(&m_tsmooth_buf[channel][i], mag);
            }

            _mm_store_ps(&m_decibels[channel][i], mag);
        }
    }

    if(m_output_channels > m_capture_channels)
        memcpy(m_decibels[1].get(), m_decibels[0].get(), outsz * sizeof(float));

    const bool slope = m_slope != 0.0f;
    if(m_stereo)
    {
        for(auto channel = 0; channel < 2; ++channel)
            for(size_t i = 0; i < outsz; ++i)
                if(slope)
                    m_decibels[channel][i] = std::clamp(m_slope * std::log10(i ? (float)i : 1.0f) + dbfs(m_decibels[channel][i]), DB_MIN, 0.0f);
                else
                    m_decibels[channel][i] = dbfs(m_decibels[channel][i]);
    }
    else if(m_capture_channels > 1)
    {
        for(size_t i = 0; i < outsz; ++i)
            if(slope)
                m_decibels[0][i] = std::clamp(m_slope * std::log10(i ? (float)i : 1.0f) + dbfs((m_decibels[0][i] + m_decibels[1][i]) / 2), DB_MIN, 0.0f);
            else
                m_decibels[0][i] = dbfs((m_decibels[0][i] + m_decibels[1][i]) / 2);
    }
    else
    {
        for(size_t i = 0; i < outsz; ++i)
            if(slope)
                m_decibels[0][i] = std::clamp(m_slope * std::log10(i ? (float)i : 1.0f) + dbfs(m_decibels[0][i]), DB_MIN, 0.0f);
            else
                m_decibels[0][i] = dbfs(m_decibels[0][i]);
    }
}
