/*
 * Copyright (c) 2019-2020, NVIDIA CORPORATION.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <rmm/cuda_stream_view.hpp>
#include <rmm/detail/aligned.hpp>

#include <cstddef>
#include <utility>

#define _CUDA_VSTD ::std

namespace rmm {

namespace mr {

using stream_view = cuda_stream_view;

/**
 * @brief Specifies the kind of memory of an allocation.
 *
 * Memory allocation kind determines where memory can be accessed and the
 * performance characteristics of accesses.
 *
 */
enum class memory_kind {
  device,  ///< Device memory accessible only from device
  unified, ///< Unified memory accessible from both host and device
  pinned,  ///< Page-locked system memory accessible from both host and device
  host     ///< System memory only accessible from host code
};

/**
 * @brief Tag type for the default context of `memory_resource`.
 *
 * Default context in which storage may be used immediately on any thread or any
 * CUDA stream without synchronization.
 */
struct any_context{};

namespace detail {
struct __empty {};

template <typename _Context>
struct __get_context_impl {
  _Context get_context() const noexcept { return do_get_context(); }
private:
  virtual _Context do_get_context() const noexcept = 0;
};

// Specialization for the default `any_context` returns a default constructed
// object and removes the virtual `do_get_context` from the class
template<>
struct __get_context_impl<any_context> {
  any_context get_context() const noexcept { return any_context{}; }
};

} // namespace detail

/**
 * @brief Abstract interface for context specific memory allocation.
 *
 * @tparam _Kind The `memory_kind` of the allocated memory.
 * @tparam _Context The execution context on which the storage may be used
 * without synchronization
 */
template <memory_kind _Kind, typename _Context = any_context>
class memory_resource : public detail::__get_context_impl<_Context> {
public:
  static constexpr memory_kind kind = _Kind;
  using context = _Context;

  static constexpr std::size_t default_alignment = alignof(_CUDA_VSTD::max_align_t);

  virtual ~memory_resource() = default;

  /**
   * @brief Returns the resource's execution context
   *
   * Inherited from base class
   *
   * context get_context() const noexcept;
   */

  /**
   * @brief Allocates storage of size at least `__bytes` bytes.
   *
   * The returned storage is aligned to the specified `__alignment` if such
   * alignment is supported.
   *
   * Storage may be accessed immediately within the execution context returned
   * by `get_context()`, otherwise synchronization is required.
   *
   * @throws If storage of the requested size and alignment cannot be obtained.
   *
   * @param __bytes The size in bytes of the allocation
   * @param __alignment The alignment of the allocation
   * @return Pointer to the requested storage
   */
  void *allocate(std::size_t __bytes,
                 std::size_t __alignment = default_alignment) {
    return do_allocate(__bytes, __alignment);
  }

  /**
   * @brief Deallocates the storage pointed to by `__p`.
   *
   * `__p` must have been returned by a prior call to `allocate(__bytes,
   * __alignment)` on a `memory_resource` that compares equal to `*this`, and
   * the storage it points to must not yet have been deallocated, otherwise
   * behavior is undefined.
   *
   * @throws Nothing.
   *
   * @param __p Pointer to storage to be deallocated
   * @param __bytes The size in bytes of the allocation. This must be equal to
   * the value of `__bytes` that was specified to the `allocate` call that
   * returned `__p`.
   * @param __alignment The alignment of the allocation. This must be equal to
   * the value of `__alignment` that was specified to the `allocate` call that
   * returned `__p`.
   */
  void deallocate(void *__p, std::size_t __bytes,
                  std::size_t __alignment = default_alignment) {
    do_deallocate(__p, __bytes, __alignment);
  }

  /**
   * @brief Compare this resource to another.
   *
   * Two resources compare equal if and only if memory allocated from one
   * resource can be deallocated from the other and vice versa.
   *
   * @param __other The other resource to compare against
   */
  bool is_equal(memory_resource const& __other) const noexcept {
    return do_is_equal(__other);
  }

private:
  virtual void *do_allocate(std::size_t __bytes, std::size_t __alignment) = 0;

  virtual void do_deallocate(void *__p, std::size_t __bytes,
                             std::size_t __alignment) = 0;

  // Default to identity comparison
  virtual bool do_is_equal(memory_resource const &__other) const noexcept{
      return this == &__other;
  }
};

#if _LIBCUDACXX_STD_VER > 14

#if __has_include(<memory_resource>)
#include <memory_resource>
#define _LIBCUDACXX_STD_PMR_NS ::std::pmr
#elif __has_include(<experimental/memory_resource>)
#include <experimental/memory_resource>
#define _LIBCUDACXX_STD_PMR_NS ::std::experimental::pmr
#endif // __has_include(<experimental/memory_resource>)

#if defined(_LIBCUDACXX_STD_PMR_NS)

namespace detail{
class __pmr_adaptor_base : public _LIBCUDACXX_STD_PMR_NS::memory_resource {
public:
  virtual cuda::memory_resource<cuda::memory_kind::host>* resource() const noexcept = 0;
};
}

template <typename _Pointer>
class pmr_adaptor final : public detail::__pmr_adaptor_base {

  using resource_type = _CUDA_VSTD::remove_reference_t<decltype(*_CUDA_VSTD::declval<_Pointer>())>;

  static constexpr bool __is_host_accessible_resource =
      _CUDA_VSTD::is_base_of_v<cuda::memory_resource<memory_kind::host>,    resource_type> or
      _CUDA_VSTD::is_base_of_v<cuda::memory_resource<memory_kind::unified>, resource_type> or
      _CUDA_VSTD::is_base_of_v<cuda::memory_resource<memory_kind::pinned>,  resource_type>;

  static_assert(
      __is_host_accessible_resource,
      "Pointer must be a pointer-like type to a type that is or derives"
      "from cuda::memory_resource whose memory_kind is host accessible");

public:
  pmr_adaptor(_Pointer __mr) : __mr_{std::move(__mr)} {}

  using raw_pointer = _CUDA_VSTD::remove_reference_t<decltype(&*_CUDA_VSTD::declval<_Pointer>())>;

  raw_pointer resource() const noexcept override { return &*__mr_; }

private:
  void *do_allocate(std::size_t __bytes, std::size_t __alignment) override {
    return __mr_->allocate(__bytes, __alignment);
  }

  void do_deallocate(void *__p, std::size_t __bytes,
                     std::size_t __alignment) override {
    return __mr_->deallocate(__p, __bytes, __alignment);
  }

  bool do_is_equal(_LIBCUDACXX_STD_PMR_NS::memory_resource const &__other) const noexcept override {
    auto __other_p = dynamic_cast<detail::__pmr_adaptor_base const *>(&__other);
    return __other_p and (__other_p->resource() == resource() or
                          __other_p->resource()->is_equal(*resource()));
  }

  _Pointer __mr_;
};
#endif // defined(_LIBCUDACXX_STD_PMR_NS)
#endif // _LIBCUDACXX_STD_VER > 14

/**
 * @brief Abstract interface for CUDA stream-ordered memory allocation.
 *
 * "Stream-ordered memory allocation" extends the CUDA programming model to
 * include memory allocation as stream-ordered operations.
 *
 * Allocating on stream `s0` returns memory that is valid to access immediately
 * only on `s0`. Accessing it on any other stream (or the host) first requires
 * synchronization with `s0`, otherwise behavior is undefined.
 *
 * Deallocating memory on stream `s1` indicates that it is valid to reuse the
 * deallocated memory immediately for another allocation on `s1`.
 *
 * Memory may be allocated and deallocated on different streams, `s0` and `s1`
 * respectively, but requires synchronization between `s0` and `s1` before the
 * deallocation occurs.
 *
 * @tparam _Kind The `memory_kind` of the allocated memory.
 */
template <memory_kind _Kind>
class stream_ordered_memory_resource : public memory_resource<_Kind /* default context */> {
public:
  using memory_resource<_Kind>::kind;
  using memory_resource<_Kind>::default_alignment;

  /**
   * @brief Allocates storage of size at least `__bytes` bytes in stream order
   * on `__stream`.
   *
   * The returned storage is aligned to `default_alignment`.
   *
   * The returned storage may be used immediately only on `__stream`. Accessing
   * it on any other stream (or the host) requires first synchronizing with
   * `__stream`.
   *
   * @throws If the storage of the requested size cannot be obtained.
   *
   * @param __bytes The size in bytes of the allocation.
   * @param __stream The stream on which to perform the allocation.
   * @return Pointer to the requested storage.
   */
  void *allocate_async(std::size_t __bytes, stream_view __stream) {
    return do_allocate_async(__bytes, default_alignment, __stream);
  }

  /**
   * @brief Allocates storage of size at least `__bytes` bytes in stream order
   * on `__stream`.
   *
   * The returned storage is aligned to the specified `__alignment` if such
   * alignment is supported.
   *
   * The returned storage may be used immediately only on `__stream`. Using it
   * on any other stream (or the host) requires first synchronizing with
   * `__stream`.
   *
   * @throws If the storage of the requested size cannot be obtained.
   *
   * @param __bytes The size in bytes of the allocation.
   * @param __alignment The alignment of the allocation
   * @param __stream The stream on which to perform the allocation.
   * @return Pointer to the requested storage.
   */
  void *allocate_async(std::size_t __bytes, std::size_t __alignment,
                       stream_view __stream) {
    return do_allocate_async(__bytes, __alignment, __stream);
  }

  /**
   * @brief Deallocates the storage pointed to by `__p` in stream order on
   * `__stream`.
   *
   * `__p` must have been returned by a prior call to
   * `allocate_async(__bytes, default_alignment)` or `allocate(__bytes,
   * default_alignment)` on a `stream_ordered_memory_resource` that compares
   * equal to `*this`, and the storage it points to must not yet have been
   * deallocated, otherwise behavior is undefined.
   *
   * Asynchronous, stream-ordered operations on `__stream` initiated before
   * `deallocate_async(__p, __bytes, __stream)` may still access the storage
   * pointed to by `__p` after `deallocate_async` returns.
   *
   * Storage deallocated on `__stream` may be reused by a future
   * call to `allocate_async` on the same stream without synchronizing
   * `__stream`. Therefore,  `__stream` is typically the last stream on which
   * `__p` was last used. It is the caller's responsibility to ensure the
   * storage pointed to by `__p` is not in use on any other stream (or the
   * host), or behavior is undefined.
   *
   * @param __p Pointer to storage to be deallocated.
   * @param __bytes The size in bytes of the allocation. This must be equal to
   * the value of `__bytes` that was specified to the `allocate` or
   * `allocate_async` call that returned `__p`.
   * @param __stream The stream on which to perform the deallocation.
   */
  void deallocate_async(void *__p, std::size_t __bytes, stream_view __stream) {
    do_deallocate_async(__p, __bytes, default_alignment, __stream);
  }

  /**
   * @brief Deallocates the storage pointed to by `__p` in stream order on
   * `__stream`.
   *
   * `__p` must have been returned by a prior call to
   * `allocate_async(__bytes, __alignment)` or `allocate(__bytes,
   * __alignment)` on a `stream_ordered_memory_resource` that compares
   * equal to `*this`, and the storage it points to must not yet have been
   * deallocated, otherwise behavior is undefined.
   *
   * Asynchronous, stream-ordered operations on `__stream` initiated before
   * `deallocate_async(__p, __bytes, __stream)` may still access the storage
   * pointed to by `__p` after `deallocate_async` returns.
   *
   * Storage deallocated on `__stream` may be reused by a future
   * call to `allocate_async` on the same stream without synchronizing
   * `__stream`. Therefore,  `__stream` is typically the last stream on which
   * `__p` was last used. It is the caller's responsibility to ensure the
   * storage pointed to by `__p` is not in use on any other stream (or the
   * host), or behavior is undefined.
   *
   * @param __p Pointer to storage to be deallocated.
   * @param __bytes The size in bytes of the allocation. This must be equal to
   * the value of `__bytes` that was specified to the `allocate` or
   * `allocate_async` call that returned `__p`.
   * @param __alignment The alignment of the allocation. This must be equal to
   * the value of `__alignment` that was specified to the `allocate` or
   * `allocate_async` call that returned `__p`.
   * @param __stream The stream on which to perform the deallocation.
   */
  void deallocate_async(void *__p, std::size_t __bytes, std::size_t __alignment,
                        stream_view __stream) {
    do_deallocate_async(__p, __bytes, __alignment, __stream);
  }

private:
  /// Default synchronous implementation of `memory_resource::do_allocate`
  void *do_allocate(std::size_t __bytes, std::size_t __alignment) override {
    auto const __default_stream = stream_view{};
    auto __p = do_allocate_async(__bytes, __alignment, __default_stream);
    __default_stream.synchronize();
    return __p;
  }

  /// Default synchronous implementation of `memory_resource::do_deallocate`
  void do_deallocate(void *__p, std::size_t __bytes,
                     std::size_t __alignment) override {
    auto const __default_stream = stream_view{};
    __default_stream.synchronize();
    do_deallocate_async(__p, __bytes, __alignment, __default_stream);
  }

  virtual void *do_allocate_async(std::size_t __bytes, std::size_t __alignment,
                                  stream_view __stream) = 0;

  virtual void do_deallocate_async(void *__p, std::size_t __bytes,
                                   std::size_t __alignment,
                                   stream_view __stream) = 0;
};

}  // namespace mr
}  // namespace rmm