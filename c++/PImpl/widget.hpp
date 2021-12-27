#include <iostream>
#include <memory>

namespace PImpl {

template<typename T>
class propagate_const {
    public:
        explicit propagate_const(T* t) : p(t) {}

        const T& operator*() const {
            return *p;
        }

        T& operator*() {
            return *p;
        }

        const T* operator->() const {
            return p;
        }

        T* operator->() {
            return p;
        }
    private:
        T* p;

};

class widget {
    public:
        void draw() const;
        void draw();
        bool show() const {
            return true;
        }
        widget(int);
        ~widget();
        widget(widget&&);
        widget& operator=(widget&&);
        widget(const widget&) = delete;
        widget& operator=(const widget&) = delete;

    private:
        class impl;
        propagate_const<impl> pImpl;

};

class widget::impl {
    public:
        void draw(const widget& w) const {
            if (w.show()) {
                std::cout << "drawing a const widget " << n << "\n";
            }
        }

        void draw(const widget& w) {
            if (w.show()) {
                std::cout << "draw a non-const widget " << n << "\n";
            }
        }
        impl(int val) : n(val) {}
    private:
        int n;
};

void widget::draw() const {
    pImpl->draw(*this);
}

void widget::draw() {
    pImpl->draw(*this);
}

widget::widget(int n) : pImpl{new impl(n)} {}
widget::widget(widget&&) = default;
widget::~widget() = default;
widget& widget::operator=(widget&&) = default;

} // namespace PImpl