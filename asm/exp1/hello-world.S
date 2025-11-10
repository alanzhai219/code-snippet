.data
    msg : .string "Hello World!\n"
    len = . - msg

.text
.global _start

_start:
    movl $len, %edx #/*参数3，字符串长度*/
    movl $msg, %ecx /*参数2，要显示的字符串*/
    movl $1, %ebx   /*参数1，文件描述符(stdout)*/
    movl $4, %eax   /*系统调用号(sys_write)*/
    int  $0x80      /*调用内核功能*/
                    /*退出程序*/

    movl $0, %ebx   /*参数1，退出代码*/
    movl $1, %eax   /*系统调用号(sys_exit)*/
    int  $0x80      /*调用内核功能*/
