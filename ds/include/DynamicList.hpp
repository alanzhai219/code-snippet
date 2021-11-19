#ifndef _DYNAMIC_LIST_HPP_
#define _DYNAMIC_LIST_HPP_

#include <cassert>
#include "SeqList.hpp"

namespace DS {

template <typename T>
class DynamicList : public SeqList {
    protected:
        int m_capacity;
    public:
        DynamicList(int capacity) {
            this->m_array = new T[capacity];
            if (this->m_array) {
                this->m_length = 0;
                m_capacity = capacity;
            }
        }

        int capacity() const {
            return this->m_capacity;
        }

        bool resize(int update_capacity) {
            assert(update_capacity > 0, "new capacity should be greater than zero.\n");
            if (update_capacity == this->capacity) {
                return true;
            }
            T* array = new T[update_capacity];
            if (array) {
                T* tmp = this->m_array;
                int len = this->m_length < update_capacity ? this->m_length : update_capacity;
                for (int i=0; i<len; ++i) {
                    array[i] = tmp[i];
                }
                this->m_array = array;
                this->m_length = len;
                this->m_capacity = update_capacity;

                delete[] tmp;
                return true;
            } else {
                std::cout << "failed new memory\n";
                return false;
            }
        }

        ~DynamicList() {
            delete[] this->m_array;
        }
};
} // DS
#endif // _DYNAMIC_LIST_HPP_