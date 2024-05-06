#include <stddef.h>
#include <memory>
#include <iostream>

template<typename T, typename U>
struct check {
  static const bool value = std::is_base_of_v<T, U> || std::is_same_v<T, U>;
};

template <typename T>
class EnableSharedFromThis;

template<typename T>
class WeakPtr;

struct BaseControlBlock {
  size_t shared_count;
  size_t weak_count;

  virtual void IncreaseShared() = 0;
  virtual void IncreaseWeak() = 0;

  size_t& GiveWeak() { return weak_count; }
  size_t& GiveShared() { return shared_count; }

  BaseControlBlock(size_t x, size_t y): shared_count(x), weak_count(y) {}
  virtual void Weak_TryToDeleteThisOne() = 0;
  virtual void Shared_TryToDeleteThisOne() = 0;

  virtual void UseDeleter() = 0;
  virtual ~BaseControlBlock() = default;
};

template <typename T>
class SharedPtr {
private:

  BaseControlBlock* cb;
  T* ptr;

public:
  template <typename U, typename... Args>
  friend SharedPtr<U> makeShared(Args&&... args);

  template <typename U, typename Alloc, typename... Args>
  friend SharedPtr<U> allocateShared(const Alloc& alloc, Args&&... args);

  template <typename U>
  friend class SharedPtr;

  template <typename U>
  friend class WeakPtr;

  template <typename U>
  friend class EnableSharedFromThis;
 
  SharedPtr(): cb(nullptr), ptr(nullptr) {}
  SharedPtr(const SharedPtr<T>& sh_ptr);

  template <typename Deleter>
  SharedPtr(T* tmp, Deleter deleter);

  SharedPtr& operator=(const SharedPtr<T>& sh_ptr);

  template <class U, std::enable_if_t<check<T, U>::value, int> = 0>
  SharedPtr(const SharedPtr<U>& sh_ptr);

  template <class U, std::enable_if_t<check<T, U>::value, int> = 0>
  SharedPtr(SharedPtr<U>&& sh_ptr);

  template <class U, std::enable_if_t<check<T, U>::value, int> = 0>
  SharedPtr(const WeakPtr<U>& sh_ptr);

  template <class U, std::enable_if_t<check<T, U>::value, int> = 0>
  SharedPtr(WeakPtr<U>&& sh_ptr);

  template <class U, class Deleter = std::default_delete<U>, class Alloc = std::allocator<U>, std::enable_if_t<check<T, U>::value, int> = 0>
  SharedPtr(U* tmp, Deleter deleter = Deleter(), Alloc alloc = Alloc());

  template <class U, std::enable_if_t<check<T, U>::value, int> = 0>
  SharedPtr& operator=(const SharedPtr<U>& sh_ptr);

  template <class U, std::enable_if_t<check<T, U>::value, int> = 0>
  SharedPtr& operator=(SharedPtr<U>&& sh_ptr);

  ~SharedPtr();

// Resets
  void reset() noexcept;

  template <typename U>
  void reset(U* tmp);
  
  template <typename U, typename Deleter>
  void reset(U* tmp, Deleter deleter);

  template <typename Y, typename Deleter, typename Alloc>
  void reset(Y* tmp, Deleter deleter, Alloc alloc);

  T* get() const noexcept { return ptr; }
  T& operator*() const noexcept { return *ptr; }
  T* operator->() const noexcept { return ptr; }

  void swap(SharedPtr& tmp) noexcept;
  size_t use_count() const { return cb->shared_count; }

  template <typename U, typename Deleter = std::default_delete<U>,
  typename Alloc = std::allocator<U>>
  struct ControlBlockRegular: public BaseControlBlock {
    U* object;
    Deleter deleter;
    Alloc alloc;

    ControlBlockRegular(U* ptr, Deleter deleter, Alloc alloc):
      BaseControlBlock(1, 0), object(ptr), deleter(deleter), alloc(alloc) {}

    virtual void UseDeleter() {
      deleter(object);
    }

    void Clean() {
      using AllocBlock = typename std::allocator_traits<Alloc>::template rebind_alloc<ControlBlockRegular<U, Deleter, Alloc>>;
      using AllocBlockTraits = std::allocator_traits<AllocBlock>;
      AllocBlock newAlloc = alloc;
      AllocBlockTraits::deallocate(newAlloc, this, 1);  
    }

    virtual void Weak_TryToDeleteThisOne() {
      --BaseControlBlock::weak_count;
      if (BaseControlBlock::weak_count == 0 && BaseControlBlock::shared_count == 0) {
        Clean();
      }
    }

    virtual void Shared_TryToDeleteThisOne() {
      --BaseControlBlock::shared_count;
      if (BaseControlBlock::shared_count == 0) {
        UseDeleter();
        if (BaseControlBlock::weak_count == 0) {
          Clean();
        }
      }
    }

    virtual void IncreaseShared() { ++BaseControlBlock::shared_count; }
    virtual void IncreaseWeak() { ++BaseControlBlock::weak_count; }

    ~ControlBlockRegular() {}
  };

  template <typename U, typename Alloc>
  struct ControlBlockMakeShared: public BaseControlBlock {
    alignas(U) char object[sizeof(U)];
    Alloc alloc;

    template <class... Args>
    ControlBlockMakeShared(const Alloc& alloc, Args&&... args): 
          BaseControlBlock(0, 0), alloc(alloc) {
      new(reinterpret_cast<U*>(object)) U(std::forward<Args>(args)...);      
    }

    void Clean() {
      using AllocBlock = typename std::allocator_traits<Alloc>::template rebind_alloc<ControlBlockMakeShared<U, Alloc>>;
      using AllocBlockTraits = std::allocator_traits<AllocBlock>;
      AllocBlock newAlloc = alloc;
      AllocBlockTraits::deallocate(newAlloc, this, 1);
    }

    virtual void UseDeleter() {
      std::allocator_traits<Alloc>::destroy(alloc, reinterpret_cast<U*>(object));
    }

    virtual void Weak_TryToDeleteThisOne() {
      --BaseControlBlock::weak_count;
      if (BaseControlBlock::weak_count == 0 && BaseControlBlock::shared_count == 0) {
        Clean();
      }
    }

   virtual void Shared_TryToDeleteThisOne() {
      --BaseControlBlock::shared_count;
      if (BaseControlBlock::shared_count == 0) {
        UseDeleter();
        if (BaseControlBlock::weak_count == 0) {
          Clean();
        }
      }
    }

    virtual void IncreaseShared() { ++BaseControlBlock::shared_count; }
    virtual void IncreaseWeak() { ++BaseControlBlock::weak_count; }

    ~ControlBlockMakeShared() {}
  };

  bool ProveDestroyed() { return ptr == nullptr && cb == nullptr; }
  SharedPtr(BaseControlBlock* cb, T* ptr): cb(cb), ptr(ptr) { cb->IncreaseShared(); }

  template<class U, typename Deleter, typename Alloc, std::enable_if_t<check<T, U>::value, int> = 0>
  void CreationRegular(U* tmp, Deleter deleter, Alloc alloc);
};

template <typename T, typename Alloc, typename... Args>
SharedPtr<T> allocateShared(Alloc alloc, Args&&... args) {
  using AllocBlock = typename std::allocator_traits<Alloc>::template rebind_alloc<typename SharedPtr<T>::template ControlBlockMakeShared<T, Alloc>>;
  using AllocBlockTraits = std::allocator_traits<AllocBlock>;
  AllocBlock newAlloc = alloc;
  typename SharedPtr<T>::template ControlBlockMakeShared<T, Alloc>* new_cb = AllocBlockTraits::allocate(newAlloc, 1);
  try {
    AllocBlockTraits::construct(newAlloc, new_cb, alloc, std::forward<Args>(args)...);
  } catch (...) {
    AllocBlockTraits::deallocate(newAlloc, new_cb, 1);
  }
  auto cb = new_cb;
  auto ptr = (reinterpret_cast<T*>(new_cb->object));
  return SharedPtr<T>(new_cb, ptr);
}

template <typename T, typename... Args>
SharedPtr<T> makeShared(Args&&... args) {
  return allocateShared<T, std::allocator<T>, Args...>(std::allocator<T>(), std::forward<Args>(args)...);
}

template<typename T>
class WeakPtr {
  template <typename U>
  friend class WeakPtr;

  BaseControlBlock* cb;
  T* ptr;

  bool ProveDestroyed() { return ptr == nullptr && cb == nullptr; }
public:

  constexpr WeakPtr() noexcept: cb(nullptr), ptr(nullptr) {}
  WeakPtr(const WeakPtr& tmp) noexcept: cb(tmp.cb), ptr(tmp.ptr) { cb->IncreaseWeak(); }
  WeakPtr(const SharedPtr<T>& tmp) noexcept: cb(tmp.cb), ptr(tmp.ptr) { cb->IncreaseWeak(); }

  WeakPtr(WeakPtr&& tmp) noexcept: cb(tmp.cb), ptr(tmp.ptr) {
    tmp.cb = nullptr;
    tmp.ptr = nullptr;
  }
  
  template <typename U, std::enable_if_t<check<T, U>::value, int> = 0>
  WeakPtr(const WeakPtr<U>& tmp) noexcept: cb(tmp.cb), ptr(static_cast<T*>(tmp.ptr)) { cb->IncreaseWeak(); } 
  
  template <typename U, std::enable_if_t<check<T, U>::value, int> = 0>
  WeakPtr(const SharedPtr<U>& tmp) noexcept: cb(tmp.cb), ptr(static_cast<T*>(tmp.ptr)) { cb->IncreaseWeak(); }

  template <typename U, std::enable_if_t<check<T, U>::value, int> = 0>
  WeakPtr(WeakPtr<U>&& tmp) noexcept: cb(tmp.cb), ptr(tmp.ptr) {
    tmp.cb = nullptr;
    tmp.ptr = nullptr;
  }
  
  bool expired() const noexcept { return cb->GiveShared() == 0; }
  
  size_t use_count() { return cb->GiveShared(); }

  SharedPtr<T> lock() const { return SharedPtr<T>(cb, ptr); }

  template <class U, std::enable_if_t<check<T, U>::value, int> = 0>
  WeakPtr& operator=(WeakPtr<U>&& tmp) noexcept;

  WeakPtr& operator=(WeakPtr&& tmp) noexcept;

  WeakPtr& operator=(const WeakPtr& tmp) noexcept;

  template <class U, std::enable_if_t<check<T, U>::value, int> = 0>
  WeakPtr& operator=(const WeakPtr<U>& tmp) noexcept;

  template <class U, std::enable_if_t<check<T, U>::value, int> = 0>
  WeakPtr& operator=(const SharedPtr<U>& tmp) noexcept;

  ~WeakPtr() {
    if (!ProveDestroyed()) {
      cb->Weak_TryToDeleteThisOne();
    }
  }
};

//  EnableSharedFromThis
template <typename T>
class EnableSharedFromThis {

  WeakPtr<T> wptr;

public:

  EnableSharedFromThis& operator=(const EnableSharedFromThis &tmp) noexcept {
    std::ignore = tmp;
    return *this;
  }

  SharedPtr<T> shared_from_this() const { return wptr.lock(); }
  WeakPtr<T> weak_from_this() const noexcept { return wptr; }
};

template <typename T>
template <class U, std::enable_if_t<check<T, U>::value, int>>
typename WeakPtr<T>::WeakPtr& WeakPtr<T>::operator=(WeakPtr<U>&& tmp) noexcept {
  if (!ProveDestroyed()) {
    cb->Weak_TryToDeleteThisOne();
  }
  cb = tmp.cb;
  ptr = static_cast<T*>(tmp.ptr);
  tmp.cb = nullptr;
  tmp.ptr = nullptr;
  return *this;
}

template <typename T>
typename WeakPtr<T>::WeakPtr& WeakPtr<T>::operator=(WeakPtr<T>&& tmp) noexcept {
  if (!ProveDestroyed()) {
    cb->Weak_TryToDeleteThisOne();
  }
  cb = tmp.cb;
  ptr = tmp.ptr;
  tmp.cb = nullptr;
  tmp.ptr = nullptr;
  return *this;
}

template <typename T>
typename WeakPtr<T>::WeakPtr& WeakPtr<T>::operator=(const WeakPtr& tmp) noexcept {
  if (!ProveDestroyed()) {
    cb->Weak_TryToDeleteThisOne();
  }
  cb = tmp.cb;
  ptr = tmp.ptr;
  cb->IncreaseWeak();
  return *this;
}

template <typename T>
template <class U, std::enable_if_t<check<T, U>::value, int>>
typename WeakPtr<T>::WeakPtr& WeakPtr<T>::operator=(const WeakPtr<U>& tmp) noexcept {
  if (!ProveDestroyed()) {
    cb->Weak_TryToDeleteThisOne();
  }
  cb = tmp.cb;
  ptr = static_cast<T*>(tmp.ptr);
  cb->IncreaseWeak();
  return *this;
}

template <typename T>
template <class U, std::enable_if_t<check<T, U>::value, int>>
typename  WeakPtr<T>::WeakPtr& WeakPtr<T>::operator=(const SharedPtr<U>& tmp) noexcept {
  if (!ProveDestroyed()) {
    cb->Weak_TryToDeleteThisOne();
  }
  cb = tmp.cb;
  ptr = tmp.ptr;
  cb->IncreaseWeak();
  return *this;
}

template <typename T>
void SharedPtr<T>::reset() noexcept {
  if (!ProveDestroyed()) {
    cb->Shared_TryToDeleteThisOne();
  }
  cb = nullptr;
  ptr = nullptr;
}
  
template <typename T>
template <typename U>
void SharedPtr<T>::reset(U* tmp) {
  if (!ProveDestroyed()) {
    cb->Shared_TryToDeleteThisOne();
  }
  ptr = reinterpret_cast<T*>(tmp);
  std::allocator<U> alloc;
  std::default_delete<U> deleter;
  CreationRegular(tmp, deleter, alloc);
}

template <typename T>
template <typename U, typename Deleter>
void SharedPtr<T>::reset(U* tmp, Deleter deleter) {
  if (!ProveDestroyed()) {
    cb->Shared_TryToDeleteThisOne();
  }
  ptr = static_cast<T*>(tmp);
  std::allocator<U> alloc;
  CreationRegular(tmp, deleter, alloc);
}

template <typename T>
template <typename Y, typename Deleter, typename Alloc>
void SharedPtr<T>::reset(Y* tmp, Deleter deleter, Alloc alloc) {
  if (!ProveDestroyed()) {
    cb->Shared_TryToDeleteThisOne();
  }
  ptr = static_cast<T*>(tmp);
  CreationRegular(tmp, deleter, alloc);
}

template <typename T>
template<class U, typename Deleter, typename Alloc, std::enable_if_t<check<T, U>::value, int>>
void SharedPtr<T>::CreationRegular(U* tmp, Deleter deleter, Alloc alloc) {
  using AllocBlock = typename std::allocator_traits<Alloc>::template rebind_alloc<ControlBlockRegular<U, Deleter, Alloc>>;
  using AllocBlockTraits = std::allocator_traits<AllocBlock>;
  AllocBlock newAlloc = alloc;
  ControlBlockRegular<U, Deleter, Alloc>* new_cb = AllocBlockTraits::allocate(newAlloc, 1);
  new(new_cb) ControlBlockRegular<U, Deleter, Alloc>(tmp, deleter, alloc);
  cb = new_cb;
}

template <typename T>
SharedPtr<T>::SharedPtr(const SharedPtr<T>& sh_ptr): cb(sh_ptr.cb), ptr(sh_ptr.ptr) {
  if (!ProveDestroyed()) {
    cb->IncreaseShared();
  }
}

template <typename T>
template <typename Deleter>
SharedPtr<T>::SharedPtr(T* tmp, Deleter deleter): cb(nullptr), ptr(tmp) {
  std::allocator<T> alloc;
  CreationRegular(tmp, deleter, alloc);
}

template <typename T>
template <class U, std::enable_if_t<check<T, U>::value, int>>
SharedPtr<T>::SharedPtr(const SharedPtr<U>& sh_ptr): cb(sh_ptr.cb), ptr(static_cast<T*>(sh_ptr.ptr)) {
  cb->IncreaseShared();
}

template <typename T>
template <class U, std::enable_if_t<check<T, U>::value, int>>
SharedPtr<T>::SharedPtr(SharedPtr<U>&& sh_ptr): cb(sh_ptr.cb), ptr(static_cast<T*>(sh_ptr.ptr)) {
  sh_ptr.cb = nullptr;
  sh_ptr.ptr = nullptr;
}

template <typename T>
template <class U, std::enable_if_t<check<T, U>::value, int>>
SharedPtr<T>::SharedPtr(const WeakPtr<U>& sh_ptr): cb(sh_ptr.cb), ptr(static_cast<T*>(sh_ptr.ptr)) {
  cb->IncreaseShared();
}

template <typename T>
template <class U, std::enable_if_t<check<T, U>::value, int>>
SharedPtr<T>::SharedPtr(WeakPtr<U>&& sh_ptr): cb(sh_ptr.cb), ptr(static_cast<T*>(sh_ptr.ptr)) {
  sh_ptr.cb = nullptr;
  sh_ptr.ptr = nullptr;
}

template <typename T>
template <class U, class Deleter, class Alloc,
          std::enable_if_t<check<T, U>::value, int>>
SharedPtr<T>::SharedPtr(U* tmp, Deleter deleter, Alloc alloc): cb(nullptr), ptr(static_cast<T*>(tmp)) {
  CreationRegular(tmp, deleter, alloc);
}

template <typename T>
void SharedPtr<T>::swap(SharedPtr& tmp) noexcept {
  std::swap(ptr, tmp.ptr);
  std::swap(cb, tmp.cb);
}

template <typename T>
template <class U, std::enable_if_t<check<T, U>::value, int>>
typename SharedPtr<T>::SharedPtr& SharedPtr<T>::operator=(const SharedPtr<U>& sh_ptr) {
    if (!ProveDestroyed()) {
      cb->Shared_TryToDeleteThisOne();
    }
    cb = sh_ptr.cb;
    ptr = static_cast<T*>(sh_ptr.ptr);
    cb->IncreaseShared();
    return *this;
  }

template <typename T>
template <class U, std::enable_if_t<check<T, U>::value, int>>
typename SharedPtr<T>::SharedPtr& SharedPtr<T>::operator=(SharedPtr<U>&& sh_ptr) {
  if (!ProveDestroyed()) {
    cb->Shared_TryToDeleteThisOne();
  }
  cb = sh_ptr.cb;
  ptr = static_cast<T*>(sh_ptr.ptr);
  sh_ptr.cb = nullptr;
  sh_ptr.ptr = nullptr;
  return *this;
}

template <typename T>
SharedPtr<T>::~SharedPtr() {
  if (!ProveDestroyed()) {
    cb->Shared_TryToDeleteThisOne();
    cb = nullptr;
    ptr = nullptr;
  }
}

template <typename T>
typename SharedPtr<T>::SharedPtr& SharedPtr<T>::operator=(const SharedPtr<T>& sh_ptr) {
  if (!ProveDestroyed()) {
    cb->Shared_TryToDeleteThisOne();
  }
  cb = sh_ptr.cb;
  ptr = sh_ptr.ptr;
  cb->IncreaseShared();
  return *this;
}
