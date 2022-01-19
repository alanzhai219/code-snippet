#include <iostream>
#include <string>

#include "register_factory.hpp"
#include "Shoe.hpp"

int main() {
    ProductRegistrar<Shoe, NikeShoe> nikeshoes("nike");
    Shoe* pNikeShoe = ProductFactory<Shoe>::GetInstance().GetProduct("nike");
    pNikeShoe->GetName();
    if (pNikeShoe) {
        delete pNikeShoe;
    }
    return EXIT_SUCCESS;
}