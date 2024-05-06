#include <type_traits>
#include <algorithm>
#include <numeric>
#include <cassert>
#include <iterator>

template<typename T>
class Deque {
public:
  template <bool is_const = false>
  struct common_iterator {
  public:
    friend class Deque<T>;

    using value_type = std::conditional_t<is_const, const T, T>;
    using pointer = std::conditional_t<is_const, const T*, T*>;
    using difference_type = int;
    using reference = std::conditional_t<is_const, const T&, T&>;
    using iterator_category = std::random_access_iterator_tag;

    common_iterator() : index_(0),  out_index_(0), it_(nullptr), out_it_(nullptr) {}

    common_iterator(int index_, int out_index_, T** out_it_, T* it_)
    : index_(index_), out_index_(out_index_), out_it_(out_it_), it_(it_) {}
    
    operator common_iterator<true>() const {
      return common_iterator<true>(index_, out_index_, out_it_, it_);
    }

    reference operator*() {
      return *it_;
    }

    pointer operator->() {
      return it_;
    }

    friend common_iterator operator+(difference_type diff, common_iterator it_now) {
      it_now += diff;
      return it_now;
    }

    friend common_iterator operator+(common_iterator it_now, difference_type diff) {
      it_now += diff;
      return it_now;
    }

    friend common_iterator operator-(common_iterator it_now, difference_type diff) {
      it_now -= diff;
      return it_now;
    }

    difference_type operator-(const common_iterator& right) const {
      return (out_index_ - right.out_index_) * size_of_chunk_ + index_ - right.index_;
    }

    bool operator==(const common_iterator& right) const {
      return  *this - right == 0;
    }

    bool operator!=(const common_iterator& right) const {
      return !(*this == right);
    }

    bool operator<(const common_iterator& right) const {
	    if (out_index_ == right.out_index_) {
        return index_ < right.index_;
      }
      return out_index_ < right.out_index_;
    }

    bool operator>(const common_iterator& right) const {
	    return right < *this;
    }

    bool operator<=(const common_iterator& right) const {
	    return !(*this > right);
    }

    bool operator>=(const common_iterator& right) const {
      return !(*this < right);
    }
	 
    common_iterator& operator+=(difference_type diff);
    common_iterator& operator-=(difference_type diff);

    common_iterator& operator++() {
      return *this += 1;
    }

    common_iterator& operator--() {
      return *this -= 1;
    }

    common_iterator operator++(int) {
      common_iterator tmp = *this;
      *this += 1;
      return tmp;
    }
    
    common_iterator operator--(int) {
      common_iterator tmp = *this;
      *this -= 1;
      return tmp;
    }

    reference operator[](int64_t diff) {
      return *(*this + diff);
    }

  private:
    int index_;
    int out_index_;
    T** out_it_;
    pointer it_;

    void swap(common_iterator right) {
      std::swap(out_it_[right.out_index_][right.index_], out_it_[out_index_][index_]);
    }
  };
	
  using iterator = common_iterator<false>;
  using const_iterator = common_iterator<true>;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  Deque& operator=(const Deque& tmp);

  explicit Deque();
  Deque(const Deque& tmp);
  explicit Deque(size_t n);
  Deque(size_t n, const T& value);

  void pop_back() {
    --end_;
    end_.it_->~T();
  }

  void pop_front() {
    begin_.it_->~T();
    ++begin_;
  }

  T& operator[](size_t index) {
    return *(begin_ + index);
  }

  const T& operator[](size_t index) const {
    return *(cbegin() + index);
  }

  T& at(size_t index);
  const T& at(size_t index) const;
  
  size_t size() const {
    return end_ - begin_;
  }

  void push_back(const T& value);
  void push_front(const T& value);
  void insert(iterator tmp, const T& value);
  void erase(const iterator& tmp);

  ~Deque() {
    for (auto it = begin_; it != end_; ++it) {
      it.it_->~T();
    }
    delete[] out_array_;
  }
	
  //Iterators	
  iterator begin() {
    return begin_;
  }

  iterator end() {
    return end_;
  }

  const_iterator cbegin() const {
    return const_iterator(begin_);
  }

  const_iterator cend() const {
    return const_iterator(end_);
  }

  const_iterator begin() const {
    return const_iterator(begin_);
  }
  
  const_iterator end() const {
    return end_;
  }

  auto rbegin() {
    return std::reverse_iterator<iterator>(end());
  }

  auto crbegin() const {
    return std::reverse_iterator<const_iterator>(cend());
  }

  auto rend() {
    return std::reverse_iterator<iterator>(begin());
  }

  auto crend() {
    return std::reverse_iterator<const_iterator>(cbegin());
  }
	
private:
  static constexpr size_t size_of_chunk_ = 32;
  size_t all_chunks_;
  iterator begin_;
  iterator end_;
  T** out_array_;

  void InitFromAnother(const Deque& tmp);
  void relocate();
};

template<typename T>
void Deque<T>::erase(const iterator& tmp) {
  for (auto i = tmp + 1; i < end_; ++i) {
    i.swap(i - 1);
  }
  pop_back();
}

template<typename T>
void Deque<T>::insert(iterator tmp, const T& value) {
  int x = tmp - begin_;
  push_back(value);
  tmp = begin_ + x;
  for (auto i = end_ - 1; i > tmp; --i) {
    i.swap(i - 1);
  }
}

template<typename T>
void Deque<T>::push_front(const T& value) {
  if (begin_.out_index_ == 0 && begin_.index_ == 0) {
    relocate();
  }
  try {
    --begin_;
    new (begin_.it_) T(value);
  } catch (...) {
    ++begin_;
    throw;
  }
}

template<typename T>
void Deque<T>::push_back(const T& value) {
  if (end_.out_index_ == (int)all_chunks_ - 1 && end_.index_ == 31) {
    relocate();
  }
  new (end_.it_) T(value);
  ++end_;
}

template<typename T>
void Deque<T>::relocate() {
  all_chunks_ *= 3;
  T** new_out_array_ = new T*[all_chunks_];
  size_t index_start = all_chunks_ / 3;
  size_t index_end = 2 * index_start;
  
  for (size_t i = 0; i < index_start; ++i) {
    new_out_array_[i] = reinterpret_cast<T*>(new char[size_of_chunk_ * sizeof(T)]);
  }

  for (size_t i = index_end; i < all_chunks_; ++i) {
    new_out_array_[i] = reinterpret_cast<T*>(new char[size_of_chunk_ * sizeof(T)]);
  }
  for (size_t i = index_start; i < index_end; ++i) {
    new_out_array_[i] = out_array_[i - index_start];
  }
  delete[] out_array_;
  out_array_ = new_out_array_;

  begin_.out_it_ = out_array_;
  begin_.out_index_ += (int)index_start;
  end_.out_index_ += (int)index_start;
  end_.out_it_ = out_array_;
}

template<typename T>
T& Deque<T>::at(size_t index) {
  if (index < 0 || index >= size()) {
    throw std::out_of_range("Error: out of range");
  }
  return *(begin_ + index);
}

template<typename T>
const T& Deque<T>::at(size_t index) const {
  if (index < 0 || index >= size()) {
    throw std::out_of_range("Error: out of range");
  }
  return *(cbegin() + index);
}

//Initialization
template<typename T>
void Deque<T>::InitFromAnother(const Deque& tmp) {
  out_array_ = new T*[all_chunks_];
  size_t last = 0;
  try {
    for (size_t i = 0; i < all_chunks_; ++i) {
      ++last;
      out_array_[i] = reinterpret_cast<T*>(new char[size_of_chunk_ * sizeof(T)]);
    }
    begin_.out_index_ = tmp.begin_.out_index_;
    begin_.out_it_ = out_array_;
    begin_.index_ = tmp.begin_.index_;
    begin_.it_ = &(out_array_[begin_.out_index_][begin_.index_]);
    end_ = begin_;

    for (;end_ - begin_ < (int)tmp.size(); ++end_) {
      new (end_.it_) T(tmp[end_ - begin_]);
    }
  } catch (...) {
    for (;end_ > begin_;--end_) {
      end_.it_->~T();
    }
    for (size_t i = 0; i < last; ++i) {
      delete[] reinterpret_cast<char*>(out_array_[i]);
    }
    throw;
  }
}

template<typename T>
Deque<T>::Deque(const Deque& tmp) : all_chunks_(tmp.all_chunks_) {
  InitFromAnother(tmp);
}

template<typename T>
typename Deque<T>::Deque<T>& Deque<T>::operator=(const Deque& tmp) {
  if (*this == &tmp) {
    return *this;
  }
  all_chunks_ = tmp.all_chunks_;
  InitFromAnother(tmp);
  return *this;
}

//Следующие три не знаю, как объединить, тк в одном случае не надо создавать,
//но надо следить за исключениями, а в другом создавать не по дефолтному конструктору,
//боюсь, что разбив на функции, они будут зависеть друг от друга и получится 
//лишь видимость оптимизации кода 
template<typename T>
Deque<T>::Deque() : all_chunks_(1 / size_of_chunk_ + 1){
  out_array_ = new T*[all_chunks_];
  size_t last = 0;
  try {
    for (size_t i = 0; i < all_chunks_; ++i) {
      ++last;
      out_array_[i] = reinterpret_cast<T*>(new char[size_of_chunk_ * sizeof(T)]);
    }
    begin_.out_index_ = 0;
    begin_.it_ = out_array_[0];
    begin_.out_it_ = out_array_;
    begin_.index_ = 0;
    end_ = begin_;
  } catch (...) {
    for (size_t i = 0; i < last; ++i) {
      delete[] reinterpret_cast<T*>(out_array_[i]);
    }
    throw;
  }
}

template<typename T>
Deque<T>::Deque(size_t n) : all_chunks_(n / size_of_chunk_ + 1) {
  out_array_ = new T*[all_chunks_];
  size_t last = 0;
  try {
    for (size_t i = 0; i < all_chunks_; ++i) {
      out_array_[i] = reinterpret_cast<T*>(new char[size_of_chunk_ * sizeof(T)]);
      ++last;
    }
    begin_.out_index_ = 0;
    begin_.it_ = out_array_[0];
    begin_.out_it_ = out_array_;
    begin_.index_ = 0;
    end_ = begin_;

    for (;end_ - begin_ < (int)n; ++end_) {
      new (end_.it_) T();
    }
  } catch (...) {
    --end_;
    for (;end_ >= begin_;--end_) {
      end_.it_->~T();
    }
    for (size_t i = 0; i < last; ++i) {
      delete[] reinterpret_cast<char*>(out_array_[i]);
    }
    delete[] out_array_;
    throw;
  }
}

template<typename T>
Deque<T>::Deque(size_t n, const T& value) : all_chunks_(n / size_of_chunk_ + 1) {
  out_array_ = new T*[all_chunks_];
  try {
    for (size_t i = 0; i < all_chunks_; ++i) {
      out_array_[i] = reinterpret_cast<T*>(new char[size_of_chunk_ * sizeof(T)]);
    }

    begin_.out_index_ = 0;
    begin_.it_ = out_array_[0];
    begin_.index_ = 0;
    begin_.out_it_ = out_array_;
    end_ = begin_;
    
    for (;end_ - begin_ < (int)n; ++end_) {
      new (end_.it_) T(value);
    }

  } catch (...) {
    for (;end_ > begin_;--end_) {
      end_.it_->~T();
    }
    for (size_t i = 0; i < all_chunks_; ++i) {
      delete[] reinterpret_cast<char*>(out_array_[i]);
    }
    throw;
  }
}

//Iterators
template<typename T>
template<bool is_const>
typename Deque<T>::common_iterator<is_const>& Deque<T>::common_iterator<is_const>::operator+=(difference_type diff) {
  if (diff < 0) {
    return *this -= -diff;
  }
  int change = diff / size_of_chunk_;
  index_ += diff % size_of_chunk_;
  if (index_ >= size_of_chunk_) {
    ++change;
    index_ -= size_of_chunk_;
  }
  out_index_ += change;
  it_ = &(out_it_[out_index_][index_]);
  return *this;
}

template<typename T>
template<bool is_const>
typename Deque<T>::common_iterator<is_const>& Deque<T>::common_iterator<is_const>::operator-=(difference_type diff) {
  if (diff < 0) {
    return *this += -diff;
  }
  out_index_ -= diff / size_of_chunk_;
  index_ -= diff % size_of_chunk_;
  if (index_ < 0) {
    --out_index_;
    index_ += size_of_chunk_;
  }
  it_ = &(out_it_[out_index_][index_]);
  return *this;
}
