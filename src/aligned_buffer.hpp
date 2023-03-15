/*
    Copyright (C) 2023 Devin Davila

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
#include <type_traits>
#include <memory>
#include <new>

// RAII uninitialized memory buffer with suitable alignment
// for data processing on the target architecture.
// 32-byte aligned on x86 with SIMD enabled,
// otherwise default alignment of operator new.

template<typename T>
class AlignedBuffer
{
public:
    static_assert(std::is_trivial_v<T>, "Only trivial types are supported");
    AlignedBuffer() = default;
    AlignedBuffer(const AlignedBuffer&) = delete;
    AlignedBuffer& operator=(const AlignedBuffer&) = delete;
    AlignedBuffer(AlignedBuffer&&) = default;
    AlignedBuffer& operator=(AlignedBuffer&&) = default;
    ~AlignedBuffer() = default;

    T& operator[](std::size_t i) const { return m_buf[i]; }
    T *get() const noexcept { return m_buf.get(); }
    explicit operator bool() const noexcept { return static_cast<bool>(m_buf); }

    void reset() { m_buf.reset(); }
    void reset(std::size_t count) { m_buf.reset(alloc(count)); }

private:
#ifndef DISABLE_X86_SIMD
    static constexpr std::size_t ALIGNMENT = ((alignof(T) > 32u) ? alignof(T) : 32u);
#else
    static constexpr std::size_t ALIGNMENT = alignof(T);
#endif // !DISABLE_X86_SIMD

    T *alloc(std::size_t count)
    {
        if constexpr (ALIGNMENT > __STDCPP_DEFAULT_NEW_ALIGNMENT__)
            return static_cast<T*>(::operator new(sizeof(T) * count, std::align_val_t{ ALIGNMENT }));
        else
            return static_cast<T*>(::operator new(sizeof(T) * count));
    }

    class Deleter
    {
    public:
        void operator()(void *p)
        {
            if constexpr (ALIGNMENT > __STDCPP_DEFAULT_NEW_ALIGNMENT__)
                ::operator delete(p, std::align_val_t{ ALIGNMENT });
            else
                ::operator delete(p);
        }
    };

    friend bool operator==(const AlignedBuffer& a, std::nullptr_t) { return a.m_buf == nullptr; }
    friend bool operator!=(const AlignedBuffer& a, std::nullptr_t) { return a.m_buf != nullptr; }

    std::unique_ptr<T[], Deleter> m_buf;
};
