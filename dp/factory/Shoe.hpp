#ifndef _SHOE_HPP_
#define _SHOE_HPP_

#include <string>
#include <iostream>

struct Shoe {
    public:
        virtual std::string GetName() = 0;
};

struct NikeShoe : public Shoe {
    public:
        NikeShoe() = default;
        NikeShoe(std::string s) {
            m_name = s;
        }
        std::string GetName() {
            std::cout << m_name << std::endl;
            return m_name;
        }
    private:
        std::string m_name = "nike shoessssssss";
    
};
#endif // _SHOE_HPP_