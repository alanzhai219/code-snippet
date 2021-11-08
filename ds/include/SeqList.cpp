#include "SeqList.hpp"

namespace DS{
    template<typename T>
    bool SeqList::insert(int idx, const T& elem) {
        bool ret = ( 0<= idx) && (idx <= m_length);
        if (ret) {
            for(auto i = m_length; i>idx; --i) {
                m_array[i+1] =  m_array[i];
            }
            m_array[idx] =  elem;
        }
        return ret;
    }

    bool SeqList::remove(int idx) {
         bool ret = (0<=idx) && (idx<=m_length);
         if (ret) {
             for (int i=idx; i<m_length; ++i) {
                 m_array[i] = m_array[i+1];
             }
             m_length -= 1;
         }
         return ret;
    }

    bool set(int idx, const T& elem) {
        bool ret = (0<=idx) && (idx <= m_length);
        if (ret) {
            m_array[idx] = elem;
        }
        return ret;
    }

    bool get(int idx, T& elem) const {
        bool ret = (0<=idx) && (idx <= m_length);
        if (ret) {
            elem = m_array[idx];
        }
        return ret;
    }

    int length() const {
        return m_length;
    }

    T& operator[](int idx) {
        bool ret = (0<=idx) && (idx <= m_length);
        if (ret) {
            return m_array[idx];
        } else {
            return const_cast<T>(-1);
        }
    }
}   // DS