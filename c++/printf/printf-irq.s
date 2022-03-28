.section .data
output:
  .ascii "Hello world\n"

.section .text
.global main
main:
  movq $4, %rax       # No.4 syscall: write
  movq $1, %rbx       # write to stdout
  movq $output, %rcx  # address of string
  movq $14, %rdx      # length of string
  int $0x80           # invoke the syscall
  movq $1, %rax       # No.1 syscall: exit
  movq $0, %rbx       # return code: 0 (success)
  int $0x80
