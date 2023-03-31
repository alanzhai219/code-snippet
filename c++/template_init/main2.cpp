#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winit-list-lifetime"

#include <iostream>
#include <iterator>

namespace at {

enum ScalarType {
    Float=0,
    Half,
    Int,
    Long,
    Double
};

template <typename T>
class ArrayRef final {
  public:
    constexpr ArrayRef() : Data(nullptr), Length(0) {}
  	constexpr ArrayRef(const std::initializer_list<T>& Vec)
      : Data(std::begin(Vec)), Length(Vec.size()) {}
    constexpr const T& operator[](size_t Index) const {
      return Data[Index];
    }
  private:
  	const T* Data;
    size_t Length;
};
}

int main() {
    // at::ArrayRef<at::ScalarType> types = {at::ScalarType::Float,
    //                                       at::ScalarType::Int};
    at::ArrayRef<int> types = {1,
                               99};
    std::cout << types[0] << "\n";
    std::cout << types[1] << "\n";
    std::cout << "=========================\n";

    std::initializer_list<at::ScalarType> types_vec = {at::ScalarType::Float,
                                                       at::ScalarType::Int};
    at::ArrayRef<at::ScalarType> types2(types_vec);
    std::cout << types2[0] << "\n";
    std::cout << types2[1] << "\n";
    return 0;
}
