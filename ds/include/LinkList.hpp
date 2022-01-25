#ifndef _LINKLIST_HPP_
#define _LINKLIST_HPP_

#include "List.hpp"
#include "utils.hpp"

namespace DS {

template <typename T>
class LinkList : public List<T> {
    public:
        struct Node {
            public:
                Node(T v, Node* n): value(v), next(n) {}
            public:
                T value;
                Node* next;
        };
        
        LinkList() {
            /*
                m_header initialized
            */
            std::cout << "Construct LinkList\n";
            m_header = new Node(0, nullptr);
            /*
                It may cause Segmenatation Fault.
                m_header->value = 0;
                m_header->next = nullptr;
            */
            m_length = 0;
        }

        bool insert(const T& elem) override {
            /* crt is short for current */
            auto* crtNode = position(m_length);
            auto* tmp = new Node(elem, nullptr);
            crtNode->next = tmp;
            m_length++;
            return true;
        }

        bool insert(int idx, const T& elem) override {
            bool ret = (0<idx) && (idx<(m_length+1));
            if (ret) {
                if (idx == (m_length+1)) {
                    insert(elem);
                } else {
                    // construct new node for insert
                    Node* newNode = new Node(elem, nullptr);
                    // Node* newNode = new Node();
                    // newNode->value = elem;
                    // newNode->next = nullptr;
                    // search for the node before idx
                    auto* crtNode = position(idx-1);
                    /*
                      ... --> crtNode -X-> nextNode --> ...
                                 (2) \    / (1)
                                     newNode
                    */
                    newNode->next = crtNode->next;
                    crtNode->next = newNode;
                }
                m_length++;
            }
            return ret;
        }

        bool remove(int idx) override {
            bool ret = (0<idx) && (idx<=m_length);
            if (ret) {
                Node* crtNode = m_header;
                crtNode = position(idx-1);
                if (idx == m_length) {
                    /*
                        search for the [m_length-1] node
                    */
                    delete crtNode->next;
                    crtNode->next = nullptr;
                } else {
                    auto* tmpNode = crtNode->next;
                    crtNode->next = crtNode->next->next;
                    delete tmpNode;
                }
                m_length--;
            }
            return ret;
        }

        bool set(int idx, const T& elem) override {
            bool ret = (0<idx) && (idx<=m_length);
            if (ret) {
                auto* crtNode = position(idx);
                crtNode->value = elem;
            }
            return ret;
        }

        bool get(int idx, T& e) const override {
            bool ret = (0<idx) && (idx<=m_length);
            if (ret) {
                auto* crtNode = position(idx);
                e = crtNode->value;
            }
            return ret;
        }
        
        int length() const override {
            return m_length;
        }

        void clear() override {
            for (int pos=1; pos<=m_length; ++pos) {
                auto* toDel = m_header->next;
                m_header->next = toDel->next;
                delete toDel;
            }
            m_length = 0;
        }

        void dump() {
            std::cout << "==========\n";
            std::cout << "m_length = " << m_length << std::endl;
            Node* crtNode = m_header;
            for (int pos=0; pos<=m_length; pos++) {
                std::cout << crtNode->value;
                if (pos != m_length) {
                    std::cout << " -> ";
                }
                crtNode = crtNode->next;
            }
            std::cout << "\n";
            std::cout << "==========\n";
        }

        ~LinkList() {
            clear();
            std::cout << "DeConstruct LinkList\n";
            delete m_header;
        }
    private:
        Node* m_header; /* define the header */
        int m_length;   /* define the length */
    private:
        /*
            func: search for index-th node
        */
        Node* position(int index) const {
            Node* ret = m_header;
            for (int pos=0; pos<index; ++pos) {
                ret = ret->next;
            }
            return ret;
        }
};
}
#endif // _LINKLIST_HPP_