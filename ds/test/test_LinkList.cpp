#include "../include/LinkList.hpp"

int main(int argc, char* argv[]) {
    DS::LinkList<int> list;
    list.insert(2);
    list.insert(3);
    list.insert(1);
    list.dump();
    list.insert(2,10);
    list.dump();
    return 0;
}