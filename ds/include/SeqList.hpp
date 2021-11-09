#ifndef _SEQLIST_H_
#define _SEQLIST_H_

#include "List.hpp"

namespace DS {

template <typename T>
class SeqList : public List<T> {
    protected:
        T* m_array;
        int m_length;
    
    public:
        bool insert(int idx, const T& elem);
        bool remove(int idx);
        bool set(int idx, const T& elem);
        bool get(int idx, T& elem) const;
        int length() const;
        bool clear();
        T& operator[](int idx);
        // virtual int capacity() const = 0;
};

}   // DS

#endif // _SEQLIST_H_