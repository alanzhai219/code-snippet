#ifndef _STATICLIST_HPP_
#define _STATICLIST_HPP_

#include "SeqList.hpp"

/* 
    static list is specified by element type and element number.
    So, type and number are necessary to construct static list.
 */
namespace DS {

template <typename T, int N>
class StaticList : public SeqList<T> {
    protected:
        T m_space[N];
    public:
        StaticList() {
            this->m_array = m_space;
            this->m_length = 0;
        }
        int capacity() const {
            return N;
        }
};

} // namespace DS
#endif // _STATICLIST_H_