#ifndef _REGISTER_FACTORY_HPP_
#define _REGISTER_FACTORY_HPP_

#include <string>
#include <iostream>
#include <map>

template<typename Product_t>
class IProductRegistrar {
    public:
        virtual Product_t* CreateProduct() = 0;
    
    protected:
        IProductRegistrar() {}
        virtual ~IProductRegistrar() {}

    private:
        IProductRegistrar(const IProductRegistrar&) = delete;
        const IProductRegistrar& operator=(const IProductRegistrar&) = delete;
};

template<typename Product_t>
class ProductFactory {
    public:
        static ProductFactory<Product_t> & GetInstance() {
            static ProductFactory<Product_t> instance;
            return instance;
        }
    
        void RegisterProduct(IProductRegistrar<Product_t> *registrar, std::string name) {
            m_ProductRegistry[name] = registrar;
        }

        Product_t* GetProduct(std::string name) {
            if (m_ProductRegistry.find(name) != m_ProductRegistry.end()) {
                return m_ProductRegistry[name]->CreateProduct();
            }
            std::cout << "No product found for " << name << std::endl;
            return nullptr;
        }

    private:
        ProductFactory() {};
        ~ProductFactory() {};

        std::map<std::string, IProductRegistrar<Product_t>*> m_ProductRegistry;
};

template<typename Product_t, typename ConcreteProduct_t>
class ProductRegistrar: public IProductRegistrar<Product_t> {
    public:
        explicit ProductRegistrar(std::string name) {
            ProductFactory<Product_t>::GetInstance().RegisterProduct(this, name);
        } 

        Product_t* CreateProduct() {
            return new ConcreteProduct_t;
        }
};
#endif // _REGISTER_FACTORY_HPP_