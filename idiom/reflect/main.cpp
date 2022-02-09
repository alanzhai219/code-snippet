#include "class_factory.hpp"
#include "test.hpp"

int main(int argc, char* argv[]) {
    ClassFactory::getInstance().registClass("Test", create_Test);
    Test* t = (Test*)ClassFactory::getInstance().getClassByName("Test");
    t->print();
    delete t;
    return 0;
}