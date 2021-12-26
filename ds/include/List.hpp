#ifndef _LIST_H_
#define _LIST_H_

namespace DS {

/*
    Abstract Class
*/
template <typename T>
class List {
    /*
    public:
        List(const& List) = delete;
        List& operator=(const& List) = delete;
    */
    public:
        List(){};
        virtual bool insert(int idx, const T& elem) = 0;
        virtual bool remove(int idx) = 0;
        virtual bool set(int idx, const T& elem) = 0;
        virtual bool get(int idx, T& e)  const = 0;
        virtual int length() const = 0;
        virtual void clear() = 0;
    protected:
        List(const& List);
        List& operator=(const& List);
};
} // DS
#endif // _LIST_H_