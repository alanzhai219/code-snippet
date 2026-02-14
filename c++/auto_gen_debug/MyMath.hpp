#pragma once

namespace MyLib {
    
    class Calculator {
    public:
        // 普通构造函数
        Calculator() {}

        // 普通方法
        int add(int a, int b);
        int subtract(int a, int b);

        // 另外一个方法，演示我们能自动处理所有成员
        double multiply(double a, double b);
    };

    class AdvancedMath {
    public:
        static int power(int base, int exp);
    };
}
