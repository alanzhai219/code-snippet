// arena.cpp
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <cassert>
class Arena {
public:
    explicit Arena(std::size_t size)
        : m_size(size) {
        // C++-17 imports std::byte in cstddef header.
        m_begin = static_cast<std::byte*>(std::malloc(size));
        if (!m_begin) {
            throw std::bad_alloc();
        }
        m_current = m_begin;
    }
    ~Arena() {
        std::free(m_begin);
    }
    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;
    Arena(Arena&& other) noexcept
        : m_begin(other.m_begin)
        , m_current(other.m_current)
        , m_size(other.m_size) {
        other.m_begin = nullptr;
        other.m_current = nullptr;
        other.m_size = 0;
    }
    Arena& operator=(Arena&& other) noexcept {
        if (this != &other) {
            std::free(m_begin);
            m_begin = other.m_begin;
            m_current = other.m_current;
            m_size = other.m_size;
            other.m_begin = nullptr;
            other.m_current = nullptr;
            other.m_size = 0;
        }
        return *this;
    }
    void* allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t)) {
        std::byte* aligned = align(m_current, alignment);
        if (aligned + size > m_begin + m_size) {
            return nullptr; // out of memory
        }
        m_current = aligned + size;
        return aligned;
    }
    template <typename T, typename... Args>
    T* create(Args&&... args) {
        void* mem = allocate(sizeof(T), alignof(T));
        if (!mem) {
            throw std::bad_alloc();
        }
        return new (mem) T(std::forward<Args>(args)...);
    }
    void reset() {
        m_current = m_begin;
    }
    std::size_t used() const {
        return static_cast<std::size_t>(m_current - m_begin);
    }
    std::size_t capacity() const {
        return m_size;
    }
private:
    static std::byte* align(std::byte* ptr, std::size_t alignment) {
        std::uintptr_t p = reinterpret_cast<std::uintptr_t>(ptr);
        std::uintptr_t aligned = (p + alignment - 1) & ~(alignment - 1);
        return reinterpret_cast<std::byte*>(aligned);
    }
private:
    std::byte* m_begin = nullptr;
    std::byte* m_current = nullptr;
    std::size_t m_size = 0;
};

int main() {
    return 0;
}