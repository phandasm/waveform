#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <vector>
#include <cassert>

// Replacement for OBS's circlebuf API
class CircularBuffer
{
public:
    CircularBuffer() { m_data.resize(4096); }
    ~CircularBuffer() = default;
    CircularBuffer(const CircularBuffer&) = delete;
    CircularBuffer& operator=(const CircularBuffer&) = delete;

    void reset() noexcept
    {
        m_pos = 0;
        m_used = 0;
    }

    size_t size() const noexcept
    {
        return m_used;
    }

    void reserve(size_t size)
    {
        if(m_data.size() >= size)
            return;

        compact();

        // NOTE: forcing a power of 2 would enable fast modulo,
        // but for now let's focus on conserving memory.
        constexpr auto align_mask = ~(size_t)1023;
        auto new_size = (size + 1024) & align_mask;
        m_data.resize(new_size);
    }

    void push_back(const void *src, size_t size)
    {
        assert(m_pos < m_data.size());
        if(size == 0) [[unlikely]]
            return;

        reserve(m_used + size);

        auto write_pos = (m_pos + m_used) % m_data.size();
        auto avail = m_data.size() - write_pos;

        if(avail >= size)
        {
            std::memcpy(m_data.data() + write_pos, src, size);
        }
        else
        {
            std::memcpy(m_data.data() + write_pos, src, avail);
            std::memcpy(m_data.data(), (const uint8_t*)src + avail, size - avail);
        }

        m_used += size;
    }

    void push_back_zero(size_t size)
    {
        assert(m_pos < m_data.size());
        if(size == 0) [[unlikely]]
            return;

        reserve(m_used + size);

        auto write_pos = (m_pos + m_used) % m_data.size();
        auto avail = m_data.size() - write_pos;

        if(avail >= size)
        {
            std::memset(m_data.data() + write_pos, 0, size);
        }
        else
        {
            std::memset(m_data.data() + write_pos, 0, avail);
            std::memset(m_data.data(), 0, size - avail);
        }

        m_used += size;
    }

    void pop_front(void *dest, size_t size)
    {
        assert(m_data.size() > 0);
        assert(m_pos < m_data.size());
        if(size == 0) [[unlikely]]
            return;
        size = std::min(size, m_used);

        auto avail = m_data.size() - m_pos;

        if(dest != nullptr)
        {
            if(avail >= size)
            {
                std::memcpy(dest, &m_data[m_pos], size);
            }
            else
            {
                std::memcpy(dest, &m_data[m_pos], avail);
                std::memcpy((uint8_t*)dest + avail, m_data.data(), size - avail);
            }
        }

        m_pos = (m_pos + size) % m_data.size();
        m_used -= size;
    }

    void peek_front(void *dest, size_t size)
    {
        assert(m_data.size() > 0);
        assert(m_pos < m_data.size());
        if(size == 0) [[unlikely]]
            return;
        if(dest == nullptr) [[unlikely]]
            return;
        size = std::min(size, m_used);

        auto avail = m_data.size() - m_pos;

        if(avail >= size)
        {
            std::memcpy(dest, &m_data[m_pos], size);
        }
        else
        {
            std::memcpy(dest, &m_data[m_pos], avail);
            std::memcpy((uint8_t*)dest + avail, m_data.data(), size - avail);
        }
    }

private:
    std::vector<uint8_t> m_data;
    size_t m_pos = 0;
    size_t m_used = 0;

    void compact()
    {
        if((m_pos + m_used) > m_data.size())
        {
            auto first = m_data.begin();
            auto last = m_data.end();
            std::rotate(first, first + m_pos, last);
            m_pos = 0;
        }
    }
};
