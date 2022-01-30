#include <iostream>
#include <utility>
#include <functional>

template <typename F>
class ScopeGuard {
public:
    explicit ScopeGuard(F && f) : m_func(std::move(f)), m_dismiss(false){}
    explicit ScopeGuard(const F& f) : m_func(f), m_dismiss(false){}

    ScopeGuard(ScopeGuard && rhs) : m_func(std::move(rhs.m_func)), m_dismiss(rhs.m_dismiss){
        rhs.Dismiss();
    }

    ~ScopeGuard() {
        std::cout << "run ~ScopeGuard\n";
        if (!m_dismiss) {
            m_func();
        }
    }


    void Dismiss() {
        std::cout << "run Dismiss\n";
        m_dismiss = true;
    }

private:
    F m_func;
    bool m_dismiss;

    ScopeGuard();
    ScopeGuard(const ScopeGuard&);
    ScopeGuard& operator=(const ScopeGuard&);
//template<class... Args>//auto Run(Args&&... args)->typename std::result_of<F(Args...)>::type//{//　　return m_func(std::forward<Args>(args)...);//}
};

template <typename F>ScopeGuard<typename std::decay<F>::type> MakeGuard(F && f){
    return ScopeGuard<typename std::decay<F>::type>(std::forward<F>(f));
}