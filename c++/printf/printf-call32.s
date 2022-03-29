.section .data
output:
  .asciz "Hello %s!\n"
name:
  .asciz "World"

.section .text
.global main
main:
  pushl $name
  pushl $output
  call printf # call printf: invoke the clib printf
  addl $8, %esp
  
  pushl $0  # push return code to stack
  call exit # call exit: invoke the clib exit
