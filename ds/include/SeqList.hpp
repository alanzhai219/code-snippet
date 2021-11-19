#ifndef _SEQLIST_HPP_
#define _SEQLIST_HPP_

#include "List.hpp"

namespace DS {

template <typename T>
class SeqList : public List<T> {
    protected:
        T* m_array;
        int m_length;
    
    public:
        // insert element
        bool insert(int idx, const T& elem) {
            bool ret = ( 0<= idx) && (idx <= m_length);
            ret = ret && (m_length < capacity())
            if (ret) {
                for(auto i = m_length; i>idx; --i) {
                    m_array[i+1] =  m_array[i];
                }
                m_array[idx] =  elem;
                m_length++;
            }
            return ret;
        }

        // remove element
        bool remove(int idx) {
            bool ret = (0<=idx) && (idx<=m_length);
            if (ret) {
                for (int i=idx; i<m_length; ++i) {
                    m_array[i] = m_array[i+1];
                }
                m_length --;
            }
            return ret;
        }

        // set element to specific position
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

        void clear() {
            m_length = 0;
        }

        T& operator[](int idx) {
            bool ret = (0<=idx) && (idx <= m_length);
            if (ret) {
                return m_array[idx];
            } else {
                return const_cast<T>(-1);
            }
        }

        T operator[](int inx) const {
            return const_cast<SeqList<T>&>(*this)[idx];
        }
        virtual int capacity() const = 0;
};
}   // DS

#endif // _SEQLIST_HPP_