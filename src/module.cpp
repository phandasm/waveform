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

#include "module.hpp"
#include "source.hpp"
#include <obs-module.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(MODULE_NAME, "en-US")

MODULE_EXPORT const char *obs_module_name()
{
    return MODULE_NAME;
}

MODULE_EXPORT const char *obs_module_description()
{
    return "Audio Spectral Analysis Plugin";
}

MODULE_EXPORT bool obs_module_load()
{
    WAVSource::register_source();
    return true;
}

MODULE_EXPORT void obs_module_unload()
{
    //...
}
