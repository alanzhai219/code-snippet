#ifndef _LIST_H_
#define _LIST_H_

namespace DS {

/*
    Abstract Class
*/
template <typename T>
class List {
    public:
        virtual bool insert(int idx, const T& elem) = 0;
        virtual bool remove(int idx) = 0;
        virtual bool set(int idx, const T& elem) = 0;
        virtual bool get(int idx, T& e)  const = 0;
        virtual int length() const = 0;
        virtual bool clear() = 0;
};
} // DS
#endif // _LIST_H_