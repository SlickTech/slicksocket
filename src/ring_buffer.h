/***
 *  MIT License
 *
 *  Copyright (c) 2019 SlickTech <support@slicktech.org>
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 */

#ifndef CORE_RING_BUFFER_H
#define CORE_RING_BUFFER_H

#include <atomic>
#include <thread>
#include <cassert>

namespace slick {
namespace core {

template<typename T>
class ring_buffer {
 public:
  class slot {
   public:
    size_t size() const noexcept { return size_; }

    T& operator[](size_t index) noexcept { return (*_buf)[_start + index]; }

    void publish(size_t num) noexcept {
      if (size_ <= 0) return;
      _buf->publish(_start, num);
      _start += num;
      size_ -= num;
    }
    void publish() noexcept {
      _buf->publish(_start, size_);
    }

    void invalidate() noexcept { _discard = true; }
    bool valid() const noexcept { return !_discard; }

   private:
    template<typename> friend class ring_buffer;

    slot() = default;
    slot(ring_buffer<T>* buf, size_t start, size_t size)
      : _start(start)
      , size_(size)
      , _buf(buf)
    {}

   private:
    size_t _start = 0;
    size_t size_ = 0;
    ring_buffer<T>* _buf = nullptr;
    bool _discard = false;
  };

  ring_buffer(size_t size)
    : buffer_(new T[size])
    , size_(size)
    , mask_(size -1)
  {
    assert(size && !(size & size -1));
  }

  virtual ~ring_buffer() {
    delete[] buffer_;
  }

  size_t size() const noexcept {
    auto cursor = cursor_.load(std::memory_order_relaxed);
    return cursor <= size_ ? cursor : size_;
  }
  size_t capacity() const noexcept { return size_; }
  bool empty()  const noexcept { return cursor_ == 0; }

  const T& operator[](size_t index) const noexcept { return buffer_[index & mask_]; }
  T& operator[](size_t index) noexcept {
    return const_cast<T&>((*static_cast<const ring_buffer<T>*>(this))[index]);
  }

  size_t available() const noexcept { return cursor_.load(std::memory_order_relaxed); }

  slot reserve(size_t num = 1) noexcept {
    return slot(this, reserved_.fetch_add(num, std::memory_order_acq_rel), num);
  }

 protected:
  friend class slot;

  ring_buffer(size_t size, bool external_buf)
      : size_(size)
      , mask_(size -1)
  {
    assert(size && !(size & size -1));
  }

  void publish(size_t index, size_t num) noexcept {
    while(cursor_.load(std::memory_order_relaxed) != index) { std::this_thread::yield(); }
    cursor_.fetch_add(num);
  }

 protected:
  T* buffer_ = nullptr;
  const size_t size_;
  const size_t mask_;
  std::atomic<size_t> cursor_ {0};
  std::atomic<size_t> reserved_ {0};
};

}
}

#endif //CORE_RING_BUFFER_H
