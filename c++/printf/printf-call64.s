.section .data
output:
  .asciz "Hello %s!\n"
name:
  .asciz "World"

.section .text
.global main
main:
  movq $output, %rdi
  movq $name, %rsi
  movq $0, %rax
  call printf # call printf: invoke the clib printf
  
  pushq $0  # push return code to stack
  call exit # call exit: invoke the clib exit
