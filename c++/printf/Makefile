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

# all: printf-std printf-write printf-syswrite printf-irq printf-syscall

build: printf-std printf-write printf-syswrite printf-irq printf-syscall
	mkdir $(BUILD)

install:
	mkdir $(BIN)
	cp $(BUILD)/*.out $(BIN)

clean:
	rm -rf bin build
