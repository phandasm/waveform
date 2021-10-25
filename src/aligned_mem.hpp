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
#include <cstddef>
#include <immintrin.h>

template<typename T>
T *avx_alloc(std::size_t num_elements)
{
    return (T*)_mm_malloc(num_elements * sizeof(T), 32);
}

static inline void avx_free(void *ptr)
{
    _mm_free(ptr);
}

class AVXDeleter
{
public:
    inline void operator()(void *p) { avx_free(p); }
};
