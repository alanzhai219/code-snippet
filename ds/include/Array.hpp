#ifndef _ARRAY_HPP_
#define _ARRAY_HPP_

namespace DS {

template <typename T>
class Array {
    protected:
        T* m_array;
    public:
        virtual bool set(int i, const T& e) {
            assert((i>=0) && (i));
        }
};
} // DS
#endif // _ARRAY_HPP_