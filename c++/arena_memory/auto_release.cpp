#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <utility>
#include <type_traits>
#include <iostream>

class Arena {
    // 定义析构函数节点的结构
    struct DestructorNode {
        void (*destructor)(void*); // 函数指针：怎么销毁
        void* object_ptr;          // 数据指针：销毁谁
        DestructorNode* next;      // 链表下一项
    };

public:
    explicit Arena(std::size_t size) : m_size(size) {
        m_begin = static_cast<std::byte*>(std::malloc(size));
        if (!m_begin) throw std::bad_alloc();
        m_current = m_begin;
    }

    ~Arena() {
        reset(); // 先析构所有对象
        std::free(m_begin); // 再释放内存池
    }

    // 禁用拷贝，支持移动（为了代码简洁，此处省略移动构造实现，同上一版）
    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;

    // 基础的字节分配（无变化）
    void* allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t)) {
        std::byte* aligned = align(m_current, alignment);
        if (aligned + size > m_begin + m_size) {
            return nullptr; 
        }
        m_current = aligned + size;
        return aligned;
    }

    // 改进后的 create 函数
    template <typename T, typename... Args>
    T* create(Args&&... args) {
        // 1. 分配对象的内存
        void* mem = allocate(sizeof(T), alignof(T));
        if (!mem) throw std::bad_alloc();

        // 2. 在内存上构造对象
        T* obj = new (mem) T(std::forward<Args>(args)...);

        // 3. 核心改进：检查是否需要记录析构函数
        // 使用 if constexpr 在编译期判断，如果是 int/float 等，这段代码会直接被优化掉，零开销！
        if constexpr (!std::is_trivially_destructible_v<T>) {
            // 额外分配一个节点来记录这个对象
            void* node_mem = allocate(sizeof(DestructorNode), alignof(DestructorNode));
            if (!node_mem) {
                // 如果节点分配失败，原则上应该手动析构 obj 并回滚，
                // 但这里简化处理，直接抛出。在 Arena 快满时分配非 POD 对象需谨慎。
                obj->~T(); 
                throw std::bad_alloc();
            }

            DestructorNode* node = new (node_mem) DestructorNode();
            
            // 设置销毁函数：利用 lambda 或 静态模板函数 封装具体的析构调用
            node->destructor = [](void* ptr) {
                static_cast<T*>(ptr)->~T();
            };
            node->object_ptr = obj;
            
            // 头插法插入链表
            node->next = m_destructor_chain;
            m_destructor_chain = node;
        }

        return obj;
    }

    void reset() {
        // 1. 遍历链表，逆序析构（后创建的先析构，符合 C++ 栈语义）
        DestructorNode* node = m_destructor_chain;
        while (node != nullptr) {
            node->destructor(node->object_ptr);
            node = node->next;
        }

        // 2. 清空链表头
        m_destructor_chain = nullptr;

        // 3. 重置内存指针
        m_current = m_begin;
    }

    std::size_t used() const { return static_cast<std::size_t>(m_current - m_begin); }

private:
    static std::byte* align(std::byte* ptr, std::size_t alignment) {
        std::uintptr_t p = reinterpret_cast<std::uintptr_t>(ptr);
        std::uintptr_t aligned = (p + alignment - 1) & ~(alignment - 1);
        return reinterpret_cast<std::byte*>(aligned);
    }

private:
    std::byte* m_begin = nullptr;
    std::byte* m_current = nullptr;
    std::size_t m_size = 0;
    
    // 析构链表的头指针
    DestructorNode* m_destructor_chain = nullptr;
};