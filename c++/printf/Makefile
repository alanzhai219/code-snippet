CC = gcc
AS = as
LD = ld

BUILD = build
BIN = bin

printf-std: printf-std.c
	gcc -o $(BUILD)/$@.out $@.c

printf-write: printf-write.c
	gcc -o $(BUILD)/$@.out $@.c

printf-syswrite: printf-syswrite.c
	gcc -o $(BUILD)/$@.out $@.c

printf-irq: printf-irq.s
	as -o $(BUILD)/$@.o $@.s
	ld -o $(BUILD)/$@.out $(BUILD)/$@.o --entry main

printf-syscall: printf-syscall.s
	as -o $(BUILD)/$@.o $@.s
	ld -o $(BUILD)/$@.out $(BUILD)/$@.o --entry main

printf-call32: printf-call32.s
	as --32 -o $(BUILD)/$@.o $^
	ld -m elf_i386 -o $(BUILD)/$@.out $(BUILD)/$@.o --entry main -lc -I/lib/ld-linux.so.2
	# gcc $^ -m32 -o $(BUILD)/$@.out

printf-call64: printf-call64.s
	as -o $(BUILD)/$@.o $^
	ld -o $(BUILD)/$@.out $(BUILD)/$@.o --entry main -lc -I/lib64/ld-linux-x86-64.so.2
	# gcc $^ -o $(BUILD)/$@.out -no-pie


# all: printf-std printf-write printf-syswrite printf-irq printf-syscall

build: printf-std printf-write printf-syswrite printf-irq printf-syscall printf-call32 printf-call64

install:
	mkdir $(BIN)
	cp $(BUILD)/*.out $(BIN)

clean:
	rm -rf bin build/*
