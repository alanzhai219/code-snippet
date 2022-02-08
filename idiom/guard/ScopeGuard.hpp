#ifndef _SCOPEGUARD_HPP_
#define _SCOPEGUARD_HPP_

#include <iostream>
#include <utility>
#include <type_traits>
#include <functional>

namespace sg {
namespace detail {

template<typename TCallback>
class ScopeGuard;

template <typename TCallback>
ScopeGuard<typename std::decay<TCallback>::type> MakeGuard(TCallback&& callback) {
    return ScopeGuard<typename std::decay<TCallback>::type>(std::forward<TCallback>(callback));
}

template<typename TCallback>
class ScopeGuard final {
    public:
        friend ScopeGuard<TCallback> MakeGuard<TCallback>(TCallback&&);
        explicit ScopeGuard(TCallback&& callback) : m_callback(std::move(callback)), m_active(false){}
        explicit ScopeGuard(const TCallback& callback) : m_callback(callback), m_active(false){}
        ScopeGuard(ScopeGuard&& rhs): m_callback(std::move(rhs.m_callback)), m_active(rhs.m_active) {
            rhs.Dismiss();
        }
    
    public:
        virtual ~ScopeGuard() noexcept {
            if (m_active) {
                m_callback();
            }
        }

        void Dismiss() noexcept {
            m_active = false;
        }


    private:
        ScopeGuard(const ScopeGuard&) = delete;
        ScopeGuard& operator=(const ScopeGuard&) = delete;
        TCallback m_callback;
        bool m_active = true;
};

}
}

#endif // _SCOPEGUARD_HPP_