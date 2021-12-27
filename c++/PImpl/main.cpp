#include <iostream>

#include "widget.hpp"

int main() {

    PImpl::widget w(7);

    const PImpl::widget w2(8);

    w.draw();
    w2.draw();
    return 0;
}