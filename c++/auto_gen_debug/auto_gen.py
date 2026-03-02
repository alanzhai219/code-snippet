import os
import sys
from pygccxml import parser, declarations

# ==========================================
# 1. 配置部分
# ==========================================

INPUT_FILE = "MyMath.hpp"
OUTPUT_FILE = "bindings.cpp"
generator_path = "/usr/bin/castxml"# 如果 castxml 不在 PATH 中，请在此填写绝对路径

xml_generator_config = parser.xml_generator_configuration_t(
    xml_generator_path=generator_path,
    xml_generator="castxml",
    cflags="-std=c++11"
)

# ==========================================
# 2. 解析部分
# ==========================================

print(f"正在解析 {INPUT_FILE} ...")

try:
    declarations_scope = parser.parse([INPUT_FILE], xml_generator_config)
except Exception as e:
    print(f"解析错误: {e}")
    sys.exit(1)

global_ns = declarations.get_global_namespace(declarations_scope)

# ==========================================
# 3. 过滤部分
# ==========================================

my_ns = global_ns.namespace("MyLib")
# 过滤掉系统库，只保留我们文件中的类
classes = my_ns.classes(lambda decl: decl.location.file_name.endswith(INPUT_FILE), recursive=False)

print(f"找到 {len(classes)} 个类需要生成绑定。")

# ==========================================
# 4. 代码生成部分
# ==========================================

lines = []
lines.append(f'#include <pybind11/pybind11.h>')
lines.append(f'#include "{INPUT_FILE}"')
lines.append('')
lines.append('namespace py = pybind11;')
lines.append('')
lines.append('PYBIND11_MODULE(my_math_ext, m) {')
lines.append('    m.doc() = "Auto-generated bindings for MyMath";')
lines.append('')

for cls in classes:
    print(f" -> 处理类: {cls.name}")
    
    lines.append(f'    py::class_<{cls.decl_string}>(m, "{cls.name}")')
    
    # --- 构造函数处理 (修复版) ---
    for ctor in cls.constructors(allow_empty=True):
        if ctor.access_type == 'public':
            # 只生成无参构造函数 (最稳健的方式)
            if len(ctor.arguments) == 0:
                lines.append(f'        .def(py::init<>())')
            # 如果需要带参构造函数，可以在这里扩展 elif len(ctor.arguments) > 0: ...

    # --- 成员函数处理 ---
    for func in cls.member_functions(allow_empty=True):
        if func.access_type == 'public' and not func.is_artificial:
            print(f"    -> 绑定方法: {func.name}")
            # 生成 .def("add", &MyLib::Calculator::add)
            lines.append(f'        .def("{func.name}", &{cls.decl_string}::{func.name})')

    lines.append('    ;') 
    lines.append('')

lines.append('}')

# ==========================================
# 5. 写入文件
# ==========================================

with open(OUTPUT_FILE, "w", encoding="utf-8") as f:
    f.write("\n".join(lines))

print(f"\n成功生成绑定代码: {OUTPUT_FILE}")
