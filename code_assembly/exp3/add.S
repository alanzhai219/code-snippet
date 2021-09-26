.global add 
add:
    pushq %rbp
    movq %rsp, %rbp
    movl %edi, -4(%rbp) /* 第一个参数，压栈 */
    movl %esi, -8(%rbp) /* 第二个参数，压栈 */   
    movl -4(%rbp), %eax /* 把第一个参数，赋值给寄存器%eax（保存返回值的寄存器）*/ 
    addl -8(%rbp), %eax /* 把第二个参数与%eax相加 */
    popq %rbp /* */
    retq /* 返回*/ 
