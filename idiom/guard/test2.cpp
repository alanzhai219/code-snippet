#include "ScopeGuard2.hpp"

void TestScopeGuard() {    
    std::function < void()> f = [] {
        std::cout << "cleanup from unnormal exit" << std::endl;
    };
    //正常退出
    {        
        auto gd = MakeGuard(f);
        std::cout << "This is Test1\n";
        //...
        gd.Dismiss();
    }

    //异常退出
    {
        auto gd = MakeGuard(f);
        std::cout << "This is Test2\n";
        //...
        // throw 1;
        // gd.Dismiss();
    }

    //非正常退出
    {
        auto gd = MakeGuard(f);
        std::cout << "This is Test3\n";
        return;
        // gd.Dismiss();
        //...
    }
}
int main() {
    TestScopeGuard();
    return 0;
}