#include <stdio.h>
inline void get_jit_code(const void* code, size_t code_size, const char* code_name) {
	if (code) {
		FILE *fp = fopen(code_name, "wb+");
		if (fp) {
			size_t code_byte = fwrite(code, code_size, 1, fp);
			fclose(fp);
		}
	}
}