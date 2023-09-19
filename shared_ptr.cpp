#include <iostream>
#include <memory>
#include <type_traits>

struct BaseControlBlock {
  virtual void del_object() = 0;
  virtual void del_ptr() = 0;
  virtual ~BaseControlBlock() = default;
  size_t shared_count = 1;
  size_t weak_count = 0;
};
template <typename T, typename Allocator = std::allocator<T>,
          typename Deleter = std::default_delete<T>>
struct ControlBlockRegular : BaseControlBlock {
  using ctrl_alloc =
      typename std::allocator_traits<Allocator>::template rebind_alloc<
          ControlBlockRegular<T, Allocator, Deleter>>;
  using ctrl_alloc_traits = std::allocator_traits<ctrl_alloc>;
  ControlBlockRegular(T* ptr, Deleter&& del = Deleter(),
                      const Allocator& alloc = Allocator())
      : ptr(ptr), alloc(alloc), del(std::move(del)) {}
  ~ControlBlockRegular() {
    // nothing
  }
  void del_object() override {
    ctrl_alloc copy_alloc = alloc;
    ctrl_alloc_traits::destroy(copy_alloc, this);
    ctrl_alloc_traits::deallocate(copy_alloc, this, 1);
  }
  void del_ptr() override { del(ptr); }
  T* ptr;
  Allocator alloc;
  Deleter del;
};

template <typename T, typename Allocator = std::allocator<T>>
struct ControlBlockMakeShared : BaseControlBlock {
  using alloc_traits = std::allocator_traits<Allocator>;
  using ctrl_alloc = typename std::allocator_traits<
      Allocator>::template rebind_alloc<ControlBlockMakeShared<T, Allocator>>;
  using ctrl_alloc_traits = std::allocator_traits<ctrl_alloc>;
  template <typename... Args>
  ControlBlockMakeShared(const Allocator& alloc, Args&&... args)
      : alloc(alloc) {
    alloc_traits::construct(this->alloc, std::addressof(object),
                            std::forward<Args>(args)...);
  }
  ~ControlBlockMakeShared() {
    // nothing
  }
  void del_object() override {
    ctrl_alloc copy_alloc = alloc;
    this->~ControlBlockMakeShared();
    ctrl_alloc_traits::deallocate(copy_alloc, this, 1);
  }
  void del_ptr() override {
    alloc_traits::destroy(alloc, std::addressof(object));
  }
  union {
    T object;
  };
  Allocator alloc;
};

template <typename T>
class SharedPtr {
 public:
  template <typename Y>
  friend class SharedPtr;
  SharedPtr() = default;
  SharedPtr(std::nullptr_t);
  template <typename Y,
            typename = std::enable_if_t<std::is_convertible_v<Y*, T*>>>
  explicit SharedPtr(Y* ptr);
  template <typename Y,
            typename = std::enable_if_t<std::is_convertible_v<Y*, T*>>>
  SharedPtr(const SharedPtr<Y>& ptr);
  SharedPtr(const SharedPtr& ptr);
  template <typename Y,
            typename = std::enable_if_t<std::is_convertible_v<Y*, T*>>>
  SharedPtr(SharedPtr<Y>&& ptr);
  SharedPtr(SharedPtr&& ptr);
  template <typename Y, typename Deleter,
            typename = std::enable_if_t<std::is_convertible_v<Y*, T*>>>
  SharedPtr(Y* ptr, Deleter del);
  template <typename Y, typename Deleter, typename Allocator,
            typename = std::enable_if_t<std::is_convertible_v<Y*, T*>>>
  SharedPtr(Y* ptr, Deleter del, Allocator alloc);
  template <typename Y,
            typename = std::enable_if_t<std::is_convertible_v<Y*, T*>>>
  SharedPtr& operator=(const SharedPtr<Y>& ptr);
  SharedPtr& operator=(const SharedPtr& ptr);
  template <typename Y,
            typename = std::enable_if_t<std::is_convertible_v<Y*, T*>>>
  SharedPtr& operator=(SharedPtr<Y>&& ptr);
  SharedPtr& operator=(SharedPtr&& ptr);
  ~SharedPtr();
  size_t use_count() const;
  T* get() const;
  T& operator*() const;
  T* operator->() const;
  void reset();
  bool operator==(const SharedPtr& ptr) const;
  void swap(SharedPtr& tmp);

  template <typename U, typename... Args>
  friend SharedPtr<U> MakeShared(Args&&... args);

  template <typename U, typename Allocator, typename... Args>
  friend SharedPtr<U> AllocateShared(const Allocator& alloc, Args&&... args);

  template <typename U>
  friend class WeakPtr;

 private:
  SharedPtr(BaseControlBlock* ctrl_b, T* ptr);
  BaseControlBlock* contorol_block_ = nullptr;
  T* object_ptr_ = nullptr;
};

template <typename T>
class WeakPtr {
 public:
  WeakPtr() = default;
  WeakPtr(const WeakPtr& ptr);
  WeakPtr(const SharedPtr<T>& ptr);
  WeakPtr(WeakPtr&& ptr);
  WeakPtr& operator=(const WeakPtr& ptr);
  WeakPtr& operator=(WeakPtr&& ptr);
  ~WeakPtr();
  bool expired() const;
  bool operator==(const WeakPtr<T>& ptr);
  SharedPtr<T> lock() const;
  void swap(WeakPtr& tmp);

 private:
  BaseControlBlock* contorol_block_ = nullptr;
  T* object_ptr_ = nullptr;
};

template <typename T>
void SharedPtr<T>::swap(SharedPtr<T>& tmp) {
  std::swap(contorol_block_, tmp.contorol_block_);
  std::swap(object_ptr_, tmp.object_ptr_);
}

template <typename T>
bool SharedPtr<T>::operator==(const SharedPtr<T>& ptr) const {
  return (object_ptr_ == ptr.object_ptr_);
}

template <typename T>
SharedPtr<T>::SharedPtr(std::nullptr_t) {}

template <typename T>
template <typename Y, typename>
SharedPtr<T>::SharedPtr(Y* ptr)
    : contorol_block_(new ControlBlockRegular<T>(ptr)), object_ptr_(ptr) {}

template <typename T>
template <typename Y, typename>
SharedPtr<T>::SharedPtr(const SharedPtr<Y>& ptr)
    : contorol_block_(ptr.contorol_block_), object_ptr_(ptr.object_ptr_) {
  if (contorol_block_ == nullptr) {
    return;
  }
  contorol_block_->shared_count += 1;
}

template <typename T>
SharedPtr<T>::SharedPtr(const SharedPtr<T>& ptr)
    : contorol_block_(ptr.contorol_block_), object_ptr_(ptr.object_ptr_) {
  if (contorol_block_ == nullptr) {
    return;
  }
  contorol_block_->shared_count += 1;
}

template <typename T>
template <typename Y, typename>
SharedPtr<T>::SharedPtr(SharedPtr<Y>&& ptr)
    : contorol_block_(ptr.contorol_block_), object_ptr_(ptr.object_ptr_) {
  ptr.contorol_block_ = nullptr;
  ptr.object_ptr_ = nullptr;
}

template <typename T>
SharedPtr<T>::SharedPtr(SharedPtr<T>&& ptr)
    : contorol_block_(ptr.contorol_block_), object_ptr_(ptr.object_ptr_) {
  ptr.contorol_block_ = nullptr;
  ptr.object_ptr_ = nullptr;
}

template <typename T>
template <typename Y, typename Deleter, typename>
SharedPtr<T>::SharedPtr(Y* ptr, Deleter del)
    : contorol_block_(new ControlBlockRegular<T, std::allocator<T>, Deleter>(
          ptr, std::move(del))),
      object_ptr_(ptr) {}

template <typename T>
template <typename Y, typename Deleter, typename Allocator, typename>
SharedPtr<T>::SharedPtr(Y* ptr, Deleter del, Allocator alloc)
    : object_ptr_(ptr) {
  using ctrl_alloc =
      typename std::allocator_traits<Allocator>::template rebind_alloc<
          ControlBlockRegular<T, Allocator, Deleter>>;
  using ctrl_alloc_traits = std::allocator_traits<ctrl_alloc>;
  ctrl_alloc alloc_c = alloc;
  ControlBlockRegular<T, Allocator, Deleter>* ptr_ctrl =
      ctrl_alloc_traits::allocate(alloc_c, 1);
  ctrl_alloc_traits::construct(alloc_c, ptr_ctrl, ptr, std::move(del), alloc);
  contorol_block_ = ptr_ctrl;
}

template <typename T>
template <typename Y, typename>
SharedPtr<T>& SharedPtr<T>::operator=(const SharedPtr<Y>& ptr) {
  if (*this == ptr) {
    return *this;
  }
  SharedPtr<T> tmp(ptr);
  swap(tmp);
  return *this;
}

template <typename T>
SharedPtr<T>& SharedPtr<T>::operator=(const SharedPtr<T>& ptr) {
  if (*this == ptr) {
    return *this;
  }
  SharedPtr<T> tmp(ptr);
  swap(tmp);
  return *this;
}

template <typename T>
template <typename Y, typename>
SharedPtr<T>& SharedPtr<T>::operator=(SharedPtr<Y>&& ptr) {
  if (*this == ptr) {
    return *this;
  }
  SharedPtr<T> tmp(std::move(ptr));
  swap(tmp);
  return *this;
}

template <typename T>
SharedPtr<T>& SharedPtr<T>::operator=(SharedPtr<T>&& ptr) {
  if (*this == ptr) {
    return *this;
  }
  SharedPtr<T> tmp(std::move(ptr));
  swap(tmp);
  return *this;
}

template <typename T>
SharedPtr<T>::~SharedPtr() {
  if (contorol_block_ == nullptr) {
    return;
  }
  contorol_block_->shared_count -= 1;
  if (contorol_block_->shared_count == 0) {
    contorol_block_->del_ptr();
    if (contorol_block_->weak_count == 0) {
      contorol_block_->del_object();
    }
  }
}

template <typename T>
size_t SharedPtr<T>::use_count() const {
  if (contorol_block_ != nullptr) {
    return contorol_block_->shared_count;
  }
  return 0;
}

template <typename T>
T* SharedPtr<T>::get() const {
  return object_ptr_;
}

template <typename T>
T& SharedPtr<T>::operator*() const {
  return *get();
}

template <typename T>
T* SharedPtr<T>::operator->() const {
  return get();
}

template <typename T>
void SharedPtr<T>::reset() {
  SharedPtr().swap(*this);
}

template <typename T>
SharedPtr<T>::SharedPtr(BaseControlBlock* ctrl_b, T* ptr)
    : contorol_block_(ctrl_b), object_ptr_(ptr) {}

template <typename T, typename... Args>
SharedPtr<T> MakeShared(Args&&... args) {
  auto ctrl_ptr = new ControlBlockMakeShared<T>(std::allocator<T>(),
                                                std::forward<Args>(args)...);
  auto sh_ptr = SharedPtr<T>(ctrl_ptr, std::addressof(ctrl_ptr->object));
  return sh_ptr;
}

template <typename T, typename Allocator, typename... Args>
SharedPtr<T> AllocateShared(const Allocator& alloc, Args&&... args) {
  using ctrl_alloc = typename std::allocator_traits<
      Allocator>::template rebind_alloc<ControlBlockMakeShared<T, Allocator>>;
  using ctrl_alloc_traits = std::allocator_traits<ctrl_alloc>;
  ctrl_alloc alloc_c = alloc;
  auto ctrl_ptr = ctrl_alloc_traits::allocate(alloc_c, 1);
  new (ctrl_ptr)
      ControlBlockMakeShared<T, Allocator>(alloc, std::forward<Args>(args)...);
  auto sh_ptr = SharedPtr<T>(ctrl_ptr, std::addressof(ctrl_ptr->object));
  return sh_ptr;
}

template <typename T>
void WeakPtr<T>::swap(WeakPtr<T>& tmp) {
  std::swap(contorol_block_, tmp.contorol_block_);
  std::swap(object_ptr_, tmp.object_ptr_);
}

template <typename T>
bool WeakPtr<T>::operator==(const WeakPtr<T>& ptr) {
  return object_ptr_ == ptr.object_ptr_;
}

template <typename T>
WeakPtr<T>::WeakPtr(const WeakPtr<T>& ptr)
    : contorol_block_(ptr.contorol_block_), object_ptr_(ptr.object_ptr_) {
  if (contorol_block_ != nullptr) {
    contorol_block_->weak_count += 1;
  }
}

template <typename T>
WeakPtr<T>::WeakPtr(const SharedPtr<T>& ptr)
    : contorol_block_(ptr.contorol_block_), object_ptr_(ptr.object_ptr_) {
  if (contorol_block_ != nullptr) {
    contorol_block_->weak_count += 1;
  }
}

template <typename T>
WeakPtr<T>::WeakPtr(WeakPtr<T>&& ptr)
    : contorol_block_(ptr.contorol_block_), object_ptr_(ptr.object_ptr_) {
  ptr.contorol_block_ = nullptr;
  ptr.object_ptr_ = nullptr;
}

template <typename T>
WeakPtr<T>& WeakPtr<T>::operator=(const WeakPtr& ptr) {
  if (*this == ptr) {
    return *this;
  }
  WeakPtr<T> tmp(ptr);
  swap(tmp);
  return *this;
}

template <typename T>
WeakPtr<T>& WeakPtr<T>::operator=(WeakPtr&& ptr) {
  if (*this == ptr) {
    return *this;
  }
  WeakPtr<T> tmp(std::move(ptr));
  swap(tmp);
  return *this;
}

template <typename T>
WeakPtr<T>::~WeakPtr() {
  if (contorol_block_ == nullptr) {
    return;
  }
  contorol_block_->weak_count -= 1;
  if (contorol_block_->shared_count == 0 && contorol_block_->weak_count == 0) {
    contorol_block_->del_object();
  }
}

template <typename T>
bool WeakPtr<T>::expired() const {
  return contorol_block_->shared_count == 0;
}

template <typename T>
SharedPtr<T> WeakPtr<T>::lock() const {
  return SharedPtr<T>(contorol_block_);
}

int main() {

  return 0;
}
