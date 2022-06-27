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

#pragma once
#include <string>
#include <sstream>
#include <obs-module.h>
#include "module.hpp"

class Log
{
private:
    std::stringstream m_stream;
    int m_loglevel;

public:
    Log(int loglevel) : m_loglevel(loglevel) { m_stream << "[" MODULE_NAME "]: "; }
    ~Log() { blog(m_loglevel, "%s", m_stream.str().c_str()); }

    template<typename T>
    Log& operator<<(const T& obj)
    {
        m_stream << obj;
        return *this;
    }
};

#define LogError Log(LOG_ERROR)
#define LogWarn Log(LOG_WARNING)
#define LogInfo Log(LOG_INFO)
#define LogDebug Log(LOG_DEBUG)
