#include <cstdio>
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
      : Data(std::begin(Vec) == std::end(Vec) ? static_cast<T*>(nullptr)
                                             : std::begin(Vec)),
        Length(Vec.size()) {}
    constexpr const T& operator[](size_t Index) const {
      return Data[Index];
    }
  private:
  	const T* Data;
    size_t Length;
};
}