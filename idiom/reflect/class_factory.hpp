#ifndef _CLASS_FACTORY_HPP_
#define _CLASS_FACTORY_HPP_

#include <iostream>
#include <string>
#include <map>

// define func ptr
typedef void* (*create_fun)();

// using create_fun = void* (*)();

class ClassFactory {
    public:
        virtual ~ClassFactory() {}

        void* getClassByName(std::string name) {
            std::map<std::string, create_fun>::iterator it = my_map.find(name);
            if (it == my_map.end()) {
                return nullptr;
            } else {
                return it->second();
            }
        }

        void registClass(std::string name, create_fun fun) {
            my_map[name] = fun;
        }

        static ClassFactory& getInstance() {
            static ClassFactory fac;
            return fac;
        }
    private:
        ClassFactory() {}
        std::map<std::string, create_fun> my_map;
};
#endif // _CLASS_FACTORY_HPP_