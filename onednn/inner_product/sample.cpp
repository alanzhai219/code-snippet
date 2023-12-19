#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>
#include <numeric>
#include <cstring>
#include <iomanip>

#include "oneapi/dnnl/dnnl.hpp"

using namespace dnnl;

using tag = memory::format_tag;
using dt = memory::data_type;

inline dnnl::memory::dim product(const dnnl::memory::dims &dims) {
    return std::accumulate(dims.begin(), dims.end(), (dnnl::memory::dim)1,
            std::multiplies<dnnl::memory::dim>());
}

void dump(std::vector<float> vec, dnnl::memory::dims dims) {
    std::cout.setf(std::ios::right);
    int size = vec.size();
    for (int i = 0; i < size; i++) {
        if (i % dims[1] == 0) {
            std::cout << "\n";
        }
        std::cout << std::setw(4) << vec[i] << " ";
    }
    std::cout << "\n";
}

void inner_product_example() {

    // Create execution dnnl::engine.
    dnnl::engine engine(dnnl::engine::kind::cpu, 0);

    // Create dnnl::stream.
    dnnl::stream engine_stream(engine);

    // Tensor dimensions.
    dnnl::memory::dim M = 2;
    dnnl::memory::dim K = 3;
    dnnl::memory::dim N = 4;

    // Keep the last dim is SAME, which is required by onednn inner_product.
    dnnl::memory::dims src_dims = {M, K}; // 2x3 - nc
    dnnl::memory::dims wgt_dims = {N, K}; // 4x3 - cn
    dnnl::memory::dims dst_dims = {M, N}; // 2x4 - nc

    // ===================== PART 1 ======================
    // ========== CREARE Infer Instance ============
    //
    // Create memory descriptors and memory objects for src and dst.
    // - what is dims?
    //   dims is to specify the matrix block in logic view.
    //   Just notify the kernel the input shape.
    // - what is layout?
    //   physical data is 1-D.
    //   layout is to notify the kernel to fetch the data as specific direction.
    //
    // For example, although the wgt_dims is NxK, the kernel will fetch real data as tag layout.
    auto src_md = memory::desc(src_dims, dt::f32, tag::nc); // ab
    auto wgt_md = memory::desc(wgt_dims, dt::f32, tag::cn); // ba, fetch data following layout
    auto dst_md = memory::desc(dst_dims, dt::f32, tag::nc); // ab

    // Create primitive post-ops (ReLU).
    primitive_attr inner_product_attr;

    // Create inner product primitive descriptor.
    auto inner_product_pd = inner_product_forward::primitive_desc(engine,
                                                                  prop_kind::forward_inference,
                                                                  src_md,
                                                                  wgt_md,
                                                                  dst_md,
                                                                  inner_product_attr);

    // Create the primitive.
    auto inner_product_prim = inner_product_forward(inner_product_pd);

    // ===================== PART 2 ======================
    // ========== write data to dnnl memory ============
    //
    // Allocate buffers.
    std::vector<float> src_data(product(src_dims));
    std::vector<float> wgt_data(product(wgt_dims));
    std::vector<float> dst_data(product(dst_dims));

    std::iota(src_data.begin(), src_data.end(), 0);
    std::iota(wgt_data.begin(), wgt_data.end(), 0);

    auto src_mem = memory(src_md, engine);
    auto wgt_mem = memory(wgt_md, engine);
    auto dst_mem = memory(dst_md, engine);

    // write_to_dnnl_memory(bias_data.data(), bias_mem);
    std::unordered_map<int, dnnl::memory> inner_product_args;
    inner_product_args[DNNL_ARG_SRC] = src_mem;
    inner_product_args[DNNL_ARG_WEIGHTS] = wgt_mem;
    inner_product_args[DNNL_ARG_DST] = dst_mem;

    // ================== PART 3 ======================
    // =========== set inputs and do infer ============
    //
    // Primitive arguments.
    inner_product_args.at(DNNL_ARG_SRC).set_data_handle(src_data.data());
    inner_product_args.at(DNNL_ARG_WEIGHTS).set_data_handle(wgt_data.data());

    // Primitive execution: inner-product with ReLU.
    inner_product_prim.execute(engine_stream, inner_product_args);

    // Wait for the computation to finalize.
    engine_stream.wait();

    // ================== PART 4 ======================
    // =========== get outputs ============
    //
    //  Of course, if you are aware of the data in dst_mem, you can access it directly
    uint8_t* dst_ptr = static_cast<uint8_t*>(dst_mem.get_data_handle());
    size_t size = dst_mem.get_desc().get_size();
    std::memcpy(dst_data.data(), dst_mem.get_data_handle(), size);

    // dump
    dump(src_data, src_dims);
    dump(wgt_data, wgt_dims);
    dump(dst_data, dst_dims);
}

int main(int argc, char **argv) {
    inner_product_example();
    return 0;
}
