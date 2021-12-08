#include <string>
#include <vector>
#include <iostream>

class Op;

template <typename T>
class Registry {
    public:
        static Registry<T>& get() {
            static Registry<T> inst;
            return inst;
        }

        T& createObj(const std::string& name) {
            T* t = new T;
            t->name = name;
            mObjs.push_back(t);
            return *t;
        }

        void print() {
            for (auto k : mObjs) {
                std::cout << 
                    "Ops: " << k->name << ", " <<
                    "desc: " << k->desc << ", " << 
                    "num_inputs: " << k->num_inputs << 
                std::endl;
            }
        }

        virtual ~Registry() {
            std::cout << "Register<T>::~Register() called" << std::endl;
            for (auto k : mObjs) {
                if (k != nullptr) {
                    delete k;
                }
            }
        }
    private:
        // private construct function means singleton pattern.
        Registry() {
            std::cout << "Registry<T>::Registry() called" << std::endl;
        }
        std::vector<T*> mObjs;
};

class Op {
    public:
        Op() {
            std::cout << "Op::OP() called" << std::endl;
        }
        
        virtual ~Op() {
            std::cout << "Op::~OP() called" << std::endl;
        }

        Op& set_num_inputs(size_t n) {
            this->num_inputs = n;
            return *this;
        }

        Op& describe(std::string desc) {
            this->desc = desc;
            return *this;
        }
    private:
        template <typename T> friend class Registry;

        std::string name;
        size_t num_inputs;
        std::string desc;
};

#define REGISTER_OP(objname, name)  \
    Op& objname = Registry<Op>::get().createObj(#name)

#define PRINT_ALL_OPS \
    Registry<Op>::get().print()