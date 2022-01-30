#ifndef _SCOPEGUARD_HPP_
#define _SCOPEGUARD_HPP_

#include <utility>
#include <type_traits>

namespace sg {
namespace detail {

template<typename Callback>
class ScopeGuard final {
    friend ScopeGuard<Callback> MakeScopeGuard<Callback>(Callback&&);
    private:
        explicit ScopeGuard(Callback callback) : m_callback(std::move(callback)){}
    
    public:
        virtual ~ScopeGuard() noexcept {
            if (m_active) {
                m_callback();
            }
        }

        void dismiss() noexcept {
            m_active = false;
        }

        ScopeGuard(const ScopeGuard&) = delete;
        ScopeGuard& operator=(const ScopeGuard&) = delete;

    private:
        Callback m_callback;
        bool m_active = true;
};
}
}

#endif // _SCOPEGUARD_HPP_