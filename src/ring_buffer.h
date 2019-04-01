/***
 *  MIT License
 *
 *  Copyright (c) 2018-2019 SlickTech <support@slicktech.org>
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
#include <cstring>
#include <algorithm>
#include <vector>

namespace slick {
namespace net {

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
    assert(size && !(size & (size - 1)));
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

class scoped_flag final {
 public:
  explicit scoped_flag(std::atomic_bool& flag) : flag_(flag) { flag_.store(true, std::memory_order_release); }
  ~scoped_flag() { flag_.store(false, std::memory_order_release); }
 private:
  std::atomic_bool& flag_;
};

class ring_string_buffer {
 private:
  enum class flag : uint8_t {
    OK = 0,
    INVALID,
    SKIP,
  };
 public:
  ring_string_buffer(size_t size)
      : buffer_(new char[size])
      , size_(size)
      , mask_(size - 1)
	  , writing_{false}
  {
    assert(size && !(size & mask_));
  }

  ~ring_string_buffer() {
    delete[] buffer_;
	buffer_ = nullptr;
  }

  ring_string_buffer(const ring_string_buffer&) = delete;
  ring_string_buffer(ring_string_buffer&&) = delete;
  ring_string_buffer& operator=(const ring_string_buffer&) = delete;
  ring_string_buffer& operator=(ring_string_buffer&&) = delete;


  bool write(const char* const msg, size_t len, size_t remaining = 0) {
    if (resetting_.load(std::memory_order_relaxed) || len == 0) {
      return false;
    }

    scoped_flag sf(writing_);
    uint32_t reset_count = reset_count_;

    // message format
    // <-- flag (1 byte) --><-- len (4 bytes) --><-- content -->

    if (writing_cursor_ == 0) {  // write a new string
      total_ = len + remaining + 5;
      remaining_ = len + remaining;
      if (total_ >= size_) {
        writing_cursor_ = 1;
        remaining_ -= len;
        skip_ = true;
        assert(false);
        return false;
      }

      writing_begin_ = writing_cursor_ = reserved_.fetch_add(total_);
      auto index = writing_begin_ & mask_;
      if ((index + total_) >= size_) {
        // remaining buffer is not enough to hold the string
        // set flag to SKIP and start from the beginning
        auto padding_sz = size_ - index;
        reserved_.fetch_add(padding_sz);

        *reinterpret_cast<flag*>(&buffer_[writing_cursor_ & mask_]) = flag::SKIP;
        _notify(padding_sz);
        writing_cursor_ = writing_begin_;
      }

      auto begin = writing_cursor_ & mask_;
      auto end = (writing_cursor_ + total_) & mask_;
      uint32_t i = 1;
      while(!resetting_.load(std::memory_order_relaxed) &&
          begin < reading_begin_.load(std::memory_order_relaxed) &&
          end > reading_begin_.load(std::memory_order_relaxed)) {
        // write and read overlap, wait for reading cursor advance
        if ((i++% 50) == 0) {
          printf("Slow consumer. begin=%zu, end=%zu, read_index=%zu, retry_count=%d\n",
                 begin,
                 end,
                 reading_begin_.load(std::memory_order_relaxed),
                 i);
        }
        std::this_thread::yield();
      }

      if (reset_count != reset_count_) {
        // buffer reset, bail out
        writing_cursor_ = 0;
        skip_ = false;
        return false;
      }

      *reinterpret_cast<flag*>(&buffer_[writing_cursor_++ & mask_]) = flag::OK;
      *reinterpret_cast<uint32_t*>(&buffer_[writing_cursor_ & mask_]) = (uint32_t)total_ - 5;
      writing_cursor_ += 4;
    }

    remaining_ -= len;
    assert(remaining_ == remaining);

    if (remaining_ == remaining && !skip_) {
      memcpy(&buffer_[writing_cursor_ & mask_], msg, len);
      writing_cursor_ += len;
    }

    if (remaining == 0) {
      if (!skip_) {
        if (remaining_ != remaining) {
          printf("message messed up, mark buffer invalid. begin=%zu", writing_begin_);
          *reinterpret_cast<flag*>(&buffer_[writing_begin_ & mask_]) = flag::INVALID;
        }
        _notify(total_);
      }
      // string completed
      writing_cursor_ = 0;
      skip_ = false;
    }
    return true;
  }

  std::pair<const char*, size_t> read() noexcept {
    auto cursor = cursor_.load(std::memory_order_relaxed);
    if (!resetting_.load(std::memory_order_relaxed)
        && reading_begin_.load(std::memory_order_relaxed) != (cursor & mask_)) {
      auto flg = *reinterpret_cast<flag*>(&buffer_[reading_cursor_++ & mask_]);
      switch (flg) {
        case flag::SKIP: {
          auto index = reading_cursor_ & mask_;
          reading_cursor_ += index ? size_ - index : 0;
          reading_begin_.store(reading_cursor_ & mask_, std::memory_order_release);
          return std::make_pair(nullptr, 0);
        }
        case flag::INVALID: {
          auto len = *reinterpret_cast<uint32_t*>(&buffer_[reading_cursor_ & mask_]);
          reading_cursor_ += len + 4;
          reading_begin_.store(reading_cursor_ & mask_, std::memory_order_release);
          return std::make_pair(nullptr, 0);
        }
        case flag::OK:
          break;
      }

      auto len = *reinterpret_cast<uint32_t*>(&buffer_[reading_cursor_ & mask_]);
      auto cur = reading_cursor_ + 4;
      reading_cursor_ += len + 4;
      const char* str = &buffer_[cur & mask_];
      reading_begin_.store(reading_cursor_ & mask_, std::memory_order_release);
      return std::make_pair(str, len);
    }
    return std::make_pair(nullptr, 0);
  }

  void reset() noexcept {
    ++reset_count_;
    scoped_flag sf(resetting_);
    while (writing_.load(std::memory_order_relaxed)); // wait for writing complete;
    reserved_.store(0, std::memory_order_relaxed);
    cursor_.store(0, std::memory_order_relaxed);
    reading_cursor_ = cursor_.load(std::memory_order_relaxed);
    writing_begin_ = 0;
    writing_cursor_ = 0;
    reading_begin_.store(reading_cursor_ & mask_, std::memory_order_release);
  }

 private:
  void _notify(size_t num) {
    while(cursor_.load(std::memory_order_relaxed) != writing_begin_) { std::this_thread::yield(); }
    cursor_.fetch_add(num);
    writing_begin_ += num;
  }

 private:
  char* buffer_;
  const size_t size_;
  const size_t mask_;

  size_t writing_cursor_ {0};
  size_t writing_begin_ {0};
  size_t reading_cursor_ {0};
  size_t total_ {0};
  size_t remaining_ {0};
  uint32_t reset_count_ {0};
  bool skip_ { false };
  std::atomic<size_t> cursor_ {0};
  std::atomic<size_t> reserved_ {0};
  std::atomic<size_t> reading_begin_ {0};
  std::atomic_bool writing_ {false};
  std::atomic_bool resetting_ {false};
};

template<typename T>
class object_pool final {
  T* data_;
  T* end_;
  std::vector<size_t> index_;
  std::atomic_size_t read_cursor_ {0};
  std::atomic_size_t write_cursor_ {0};
  const size_t size_;
  const size_t mask_;

 public:
  object_pool(size_t size)
    : data_(new T[size])
    , end_(data_ + size)
    , index_(size)
    , size_(size)
    , mask_(size - 1) {
    assert(size && !(size & (size - 1))); // must power of 2
    std::generate(index_.begin(), index_.end(), [n = 0]() mutable { return n++; });
  }

  ~object_pool() {
    delete [] data_;
    data_ = nullptr;
  }

  object_pool(const object_pool&) = delete;
  object_pool(object_pool&&) = delete;
  object_pool& operator=(const object_pool&) = delete;
  object_pool& operator=(object_pool&&) = delete;

  size_t size() const noexcept { return size_; }

  T* get_obj() noexcept {
    if (read_cursor_.load(std::memory_order_relaxed) < mask_) {
      auto i = read_cursor_.fetch_add(1, std::memory_order_acq_rel);
      if (i < size_ || (i & mask_) < (write_cursor_.load(std::memory_order_relaxed) & mask_)) {
        return data_ + index_[i & mask_];
      }
      read_cursor_.fetch_sub(1, std::memory_order_acq_rel);
    }

    // all object pre-allocated are consumed, allocated on the heap
    return new (std::nothrow) T;
  }

  void release_obj(T* obj) noexcept {
    if (obj >= data_ && obj < end_) {
      // this is pre-allocated object
      auto index = obj - data_;
      auto target = write_cursor_.fetch_add(1, std::memory_order_acq_rel);
      index_[target & mask_] = index;
      return;
    }
    // delete object allocated on the heap
    delete obj;
  }
};

}
}

#endif //CORE_RING_BUFFER_H
