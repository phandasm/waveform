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
#include "waveform_config.hpp"
#include <cstddef>

// Abstraction for allocating memory buffers with suitable
// alignment for data processing on the target architecture.
// On x86 with SIMD enabled, returns 32-byte aligned buffers,
// otherwise defaults to malloc.

#ifndef DISABLE_X86_SIMD
#include "aligned_mem.hpp"

template<typename T>
T *membuf_alloc(std::size_t num_elements)
{
    return avx_alloc<T>(num_elements);
}

using MembufDeleter = AVXDeleter;

#else
#include <cstdlib>

template<typename T>
T *membuf_alloc(std::size_t num_elements)
{
    return (T*)std::malloc(num_elements * sizeof(T));
}

class MembufDeleter
{
public:
    inline void operator()(void *p) { std::free(p); }
};

#endif // !DISABLE_X86_SIMD
