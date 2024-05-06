#include <type_traits>
#include <numeric>
#include <cassert>
#include <iterator>
#include <tuple>
#include <memory>

template<size_t N>
class StackStorage {
public:
  char data_[N];
  size_t capacity = 0;

  StackStorage() = default;
  StackStorage(const StackStorage& tmp) = delete;
  ~StackStorage() = default;
};


template<typename T, size_t N>
class StackAllocator {
public:
  using value_type = T;
  T* allocate(size_t count);
  
  void deallocate(T* ptr, size_t count) {
    std::ignore = ptr;
    std::ignore = count;
  }

  template <typename U>
  struct rebind {
    using other = StackAllocator<U, N>;
  };

  template <typename U>
  StackAllocator(const StackAllocator<U, N>& tmp) {
    storage = tmp.storage;
  }

  template <size_t M>
  StackAllocator(StackStorage<M>& tmp) {
    storage = &tmp;
  }

  StackStorage<N>* storage;
};

template<typename T, size_t N>
T* StackAllocator<T, N>::allocate(size_t count) {
  void* ptr = static_cast<void*>(storage->capacity + storage->data_);
  size_t mx = N - storage->capacity;
  size_t last_mx = mx;
  if (std::align(alignof(T), sizeof(T) * count, ptr, mx)) {
    T* result = reinterpret_cast<T*>(ptr);
    storage->capacity += sizeof(T) * count + last_mx - mx;
    return result;
  }
  throw std::bad_alloc();
}

template<typename T, size_t N>
bool operator==(const StackAllocator<T, N>& left, const StackAllocator<T, N>& right) {
  return left.storage == right.storage;
}

template<typename T, size_t N>
bool operator!=(const StackAllocator<T, N>& left, const StackAllocator<T, N>& right) {
  return left.storage != right.storage;
}

template<typename T, typename Alloc = std::allocator<T>>
class List {
private:
  struct BaseNode {
    BaseNode* next;
    BaseNode* prev;
  };

  struct Node: public BaseNode {
    T value;
    Node() = default;
    Node(const T& tmp) : value(tmp) {}
  };

  using NodeAlloc = typename std::allocator_traits<Alloc>::template rebind_alloc<Node>;
  using NodeAllocTraits = std::allocator_traits<NodeAlloc>;
  NodeAlloc alloc_;
  size_t size_;
  BaseNode fakeNode_;

  void Initialization(size_t n, const T& value);
  void TypicalInitialization(size_t n);
public:
  using AllocTraits = std::allocator_traits<Alloc>;

  template<bool is_const = false>
  struct common_iterator {
    using value_type = std::conditional_t<is_const, const T, T>;
    using pointer = std::conditional_t<is_const, const T*, T*>;
    using difference_type = int;
    using reference = std::conditional_t<is_const, const T&, T&>;
    using iterator_category = std::bidirectional_iterator_tag;

    BaseNode* node;

    common_iterator() = default;
    common_iterator(BaseNode* tmp) : node(tmp) {}
    common_iterator(const BaseNode* tmp) : node(const_cast<BaseNode*>(tmp)) {}
    
    operator common_iterator<true>() const {
      return common_iterator<true>(node);
    }

    common_iterator& operator++() {
      node = node->next;
      return *this;
    }

    common_iterator& operator--() {
      node = node->prev;
      return *this;
    }

    common_iterator operator++(int) {
      common_iterator tmp = *this;
      node = node->next;
      return tmp;
    }

    common_iterator operator--(int) {
      common_iterator tmp = *this;
      node = node->prev;
      return tmp;
    }

    bool operator==(const common_iterator& tmp) const {
      return node == tmp.node;
    }

    bool operator!=(const common_iterator& tmp) const {
      return node != tmp.node;
    }

    reference operator*() {
      return (static_cast<Node*>(node)->value);
    }

    pointer operator->() {
      return &(static_cast<Node*>(node)->value);
    }
  };

  using iterator = common_iterator<false>;
  using const_iterator = common_iterator<true>;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  List() : size_(0) {
    fakeNode_.next = &fakeNode_;
    fakeNode_.prev = &fakeNode_;
  }

  List(size_t n, const T& value);
  explicit List(size_t n);
  List(const Alloc& tmp_alloc);
  List(size_t n, const T& value, Alloc tmp_alloc);
  List(size_t n, Alloc tmp_alloc);
  List(const List<T, Alloc>& lst);

  NodeAlloc& get_allocator() {
    return alloc_;
  }

  void insert(const_iterator pos);
  void insert(const_iterator pos, const T& value);
  void erase(const_iterator pos);

  iterator begin() {
    iterator tmp = end();
    ++tmp;
    return tmp;
  }

  iterator end() {
    return iterator(&fakeNode_);
  }

  const_iterator cbegin() const {
    const_iterator it = cend();
    ++it;
    return it;
  }

  const_iterator cend() const {
    return const_iterator(&fakeNode_);
  }

  const_iterator begin() const {
    return cbegin();
  }

  const_iterator end() const {
    return cend();
  }

  auto rbegin() {
    return std::reverse_iterator<iterator>(end());
  }

  const_reverse_iterator rbegin() const {
    return std::reverse_iterator<const_iterator>(end());
  }

  const_reverse_iterator rend() const {
    return std::reverse_iterator<const_iterator>(begin());
  }

  auto crbegin() const {
    return std::reverse_iterator<const_iterator>(cend());
  }

  auto rend() {
    return std::reverse_iterator<iterator>(begin());
  }

  auto crend() const {
    return std::reverse_iterator<const_iterator>(cbegin());
  }

  void push_back(const T& value) {
    insert(end(), value);
  }

  void push_front(const T& value) {
    insert(begin(), value);
  }

  void pop_back() {
    common_iterator tmp = --end();
    erase(tmp);
  }

  void pop_front() {
    erase(begin());
  }

  size_t size() const {
    return size_;
  }

  List& operator=(const List<T, Alloc>& lst);

  ~List() {
    while (fakeNode_.prev != &fakeNode_) {
      pop_back();
    }
  }
};

template<typename T, typename Alloc>
void List<T, Alloc>::insert(const_iterator pos, const T& value) {
  Node* tmp = NodeAllocTraits::allocate(alloc_, 1);
  try {
    NodeAllocTraits::construct(alloc_, tmp, value);
  } catch (...) {
    NodeAllocTraits::deallocate(alloc_, tmp, 1);
    throw;
  }
  ++size_;

  BaseNode* after = pos.node;
  --pos;
  BaseNode* before = pos.node;

  before->next = tmp;
  after->prev = tmp;
  tmp->next = after;
  tmp->prev = before;
}

template<typename T, typename Alloc>
void List<T, Alloc>::insert(const_iterator pos) {
  Node* tmp = NodeAllocTraits::allocate(alloc_, 1);
  try {
    NodeAllocTraits::construct(alloc_, tmp);
  } catch (...) {
    NodeAllocTraits::deallocate(alloc_, tmp, 1);
    throw;
  }
  ++size_;

  BaseNode* after = pos.node;
  --pos;
  BaseNode* before = pos.node;

  before->next = tmp;
  after->prev = tmp;
  tmp->next = after;
  tmp->prev = before;
}

template<typename T, typename Alloc>
void List<T, Alloc>::erase(const_iterator pos) {
  --pos;
  BaseNode* before = pos.node;
  ++pos;
  ++pos;
  BaseNode* after = pos.node;
  --pos;

  Node* tmp = static_cast<Node*>(pos.node);
  NodeAllocTraits::destroy(alloc_, tmp);
  NodeAllocTraits::deallocate(alloc_, tmp, 1);
  before->next = after;
  after->prev = before;
  --size_;
}


template<typename T, typename Alloc>
typename List<T, Alloc>::List<T, Alloc>& List<T, Alloc>::operator=(const List<T, Alloc>& lst) {
  size_t last_size = size_;
  size_t new_size = 0;
  if (std::allocator_traits<Alloc>::propagate_on_container_copy_assignment::value) {
    alloc_ = lst.alloc_;
  }
  try {
    for (auto& it : lst) {
      insert(end(), it);
      ++new_size;
    }
  } catch (...) {
    for (size_t i = 0; i < new_size; ++i) {
      pop_back();
    }
    throw;
  }
  for (size_t i = 0; i < last_size; ++i) {
    erase(begin());
  }
  return *this;
}

//constructors
template<typename T, typename Alloc>
void List<T, Alloc>::TypicalInitialization(size_t n) {
  fakeNode_.next = &fakeNode_;
  fakeNode_.prev = &fakeNode_;
  
  size_t last_change = 0;
  try {
    for (; last_change < n; ++last_change) {
      insert(begin());
    }
  } catch (...) {
    for (size_t i = 0; i < last_change; ++i) {
      erase(begin());
    }
    throw;
  }
}

template<typename T, typename Alloc>
void List<T, Alloc>::Initialization(size_t n, const T& value) {
  fakeNode_.next = &fakeNode_;
  fakeNode_.prev = &fakeNode_;
  
  size_t last_change = 0;
  try {
    for (; last_change < n; ++last_change) {
      insert(begin(), value);
    }
  } catch (...) {
    for (size_t i = 0; i < last_change; ++i) {
      erase(begin());
    }
    throw;
  }
}

template<typename T, typename Alloc>
List<T, Alloc>::List(const Alloc& tmp_alloc) : alloc_(std::allocator_traits<NodeAlloc>::select_on_container_copy_construction(tmp_alloc)), size_(0) {
  fakeNode_.next = &fakeNode_;
  fakeNode_.prev = &fakeNode_;
}

template<typename T, typename Alloc>
List<T, Alloc>::List(size_t n, const T& value) : size_(0) {
  Initialization(n, value);
}

template<typename T, typename Alloc>
List<T, Alloc>::List(size_t n) : size_(0) {
  TypicalInitialization(n);
}

template<typename T, typename Alloc>
List<T, Alloc>::List(size_t n, const T& value, Alloc tmp_alloc) : alloc_(std::allocator_traits<NodeAlloc>::select_on_container_copy_construction(tmp_alloc)), size_(0) {
  Initialization(n, value);
}

template<typename T, typename Alloc>
List<T, Alloc>::List(size_t n, Alloc tmp_alloc) : alloc_(std::allocator_traits<NodeAlloc>::select_on_container_copy_construction(tmp_alloc)), size_(0) {
  TypicalInitialization(n);
}

template<typename T, typename Alloc>
List<T, Alloc>::List(const List<T, Alloc>& lst) : alloc_(std::allocator_traits<NodeAlloc>::select_on_container_copy_construction(lst.alloc_)), size_(0) {
  fakeNode_.next = &fakeNode_;
  fakeNode_.prev = &fakeNode_;
  try {
    for (auto& it : lst) {
      insert(end(), it);
    }
  } catch (...) {
    while (fakeNode_.prev != &fakeNode_) {
      erase(begin());
    }
    throw;
  }
}
