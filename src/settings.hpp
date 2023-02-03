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
#include <obs-module.h>
#include <cstring>

// convenience macro for getting localized text
#define T(text) obs_module_text(text)

// convenience function for testing equality of raw C strings
static inline bool p_equ(const char *s1, const char *s2) { return std::strcmp(s1, s2) == 0; }

// localizable property strings
#define P_AUDIO_SRC         "audio_source"
#define P_NONE              "none"
#define P_OUTPUT_BUS        "output_bus"

#define P_HIDE_SILENT       "hide_on_silent"

#define P_NORMALIZE_VOLUME  "normalize_volume"

#define P_RENDER_MODE       "render_mode"
#define P_LINE              "line"
#define P_SOLID             "solid"
#define P_GRADIENT          "gradient"

#define P_WIDTH             "width"
#define P_HEIGHT            "height"

#define P_LOG_SCALE         "log_scale"

#define P_MIRROR_FREQ_AXIS  "mirror_freq_axis"

#define P_RADIAL            "radial_layout"
#define P_INVERT            "invert_direction"
#define P_DEADZONE          "deadzone"
#define P_RADIAL_ARC        "radial_arc"

#define P_CAPS              "rounded_caps"

#define P_WINDOW            "window"
#define P_HANN              "hann"
#define P_HAMMING           "hamming"
#define P_BLACKMAN          "blackman"
#define P_BLACKMAN_HARRIS   "blackman_harris"

#define P_AUTO_FFT_SIZE     "auto_fft_size"
#define P_FFT_SIZE          "fft_size"

#define P_CHANNEL_MODE      "channel_mode"
#define P_MONO              "mono"
#define P_STEREO            "stereo"
#define P_SINGLE            "single"

#define P_CHANNEL           "channel"

#define P_CHANNEL_SPACING   "channel_spacing"

#define P_INTERP_MODE       "interp_mode"
#define P_POINT             "point"
#define P_LANCZOS           "lanczos"

#define P_FILTER_MODE       "filter_mode"
#define P_FILTER_RADIUS     "filter_radius"
#define P_GAUSS             "gauss"

#define P_CUTOFF_LOW        "cutoff_low"
#define P_CUTOFF_HIGH       "cutoff_high"
#define P_FLOOR             "floor"
#define P_CEILING           "ceiling"
#define P_SLOPE             "slope"
#define P_ROLLOFF_Q         "rolloff_q"
#define P_ROLLOFF_RATE      "rolloff_rate"

#define P_GRAVITY           "gravity"
#define P_TSMOOTHING        "temporal_smoothing"
#define P_EXPAVG            "exp_moving_avg"
#define P_FAST_PEAKS        "fast_peaks"

#define P_COLOR_BASE        "color_base"
#define P_COLOR_CREST       "color_crest"
#define P_GRAD_RATIO        "grad_ratio"

#define P_DISPLAY_MODE      "display_mode"
#define P_CURVE             "curve"
#define P_BARS              "bars"
#define P_STEP_BARS         "stepped_bars"
#define P_LEVEL_METER       "level_meter"
#define P_STEPPED_METER     "stepped_level_meter"

#define P_RMS_MODE          "rms_mode"
#define P_METER_BUF         "meter_buf"

#define P_BAR_WIDTH         "bar_width"
#define P_BAR_GAP           "bar_gap"
#define P_STEP_WIDTH        "step_width"
#define P_STEP_GAP          "step_gap"
#define P_MIN_BAR_HEIGHT    "min_bar_height"


// tooltip descriptions
#define P_CHAN_DESC         "chan_desc"
#define P_AUTO_FFT_DESC     "auto_fft_desc"
#define P_FFT_DESC          "fft_desc"
#define P_WINDOW_DESC       "window_desc"
#define P_TEMPORAL_DESC     "temporal_desc"
#define P_GRAVITY_DESC      "gravity_desc"
#define P_FAST_PEAKS_DESC   "fast_peaks_desc"
#define P_INTERP_DESC       "interp_desc"
#define P_FILTER_DESC       "filter_desc"
#define P_SLOPE_DESC        "slope_desc"
#define P_DEADZONE_DESC     "deadzone_desc"
#define P_CAPS_DESC         "caps_desc"
#define P_ROLLOFF_Q_DESC    "rolloff_q_desc"
#define P_ROLLOFF_RATE_DESC "rolloff_rate_desc"
#define P_VOLUME_NORM_DESC  "volume_normalization_desc"
#define P_MIRROR_DESC       "mirror_desc"
#define P_RADIAL_ARC_DESC   "radial_arc_desc"
