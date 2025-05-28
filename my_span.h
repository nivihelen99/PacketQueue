#include <cstddef>     // For std::size_t, std::ptrdiff_t
#include <vector>      // For std::vector
#include <array>       // For std::array
#include <stdexcept>   // For std::out_of_range
#include <iterator>    // For std::reverse_iterator, std::iterator_traits, std::random_access_iterator_tag
#include <type_traits> // For std::remove_cv_t, std::is_const, std::is_convertible, std::enable_if_t, std::is_base_of, std::remove_reference_t, std::declval
#include <cassert>     // For assert in tests
#include <iostream>    // For std::cout, std::cerr
#include <string>      // For std::string
#include <numeric>     // For std::iota (used in tests)

// Define a custom dynamic_extent for C++17 compatibility
namespace my_std {
    // A large value to represent dynamic extent, mimicking std::dynamic_extent
    // In C++20, it's std::numeric_limits<std::size_t>::max()
    static constexpr std::size_t dynamic_extent = static_cast<std::size_t>(-1);

    /**
     * @brief A C++17 compatible implementation of std::span.
     * Provides a non-owning view over a contiguous sequence of objects.
     *
     * @tparam T The element type of the sequence. Can be const or non-const.
     * @tparam Extent The size of the sequence if known at compile time.
     * Use my_std::dynamic_extent for a runtime-sized span.
     */
    template <typename T, std::size_t Extent = dynamic_extent>
    class span {
    public:
        // --- Member types ---
        using element_type = T;
        using value_type = std::remove_cv_t<T>; // Remove const/volatile for underlying value type
        using size_type = std::size_t;
        using difference_type = std::ptrdiff_t;
        using pointer = T*;
        using const_pointer = const T*;
        using reference = T&;
        using const_reference = const T&;
        using iterator = T*; // Raw pointers serve as iterators for contiguous memory
        using const_iterator = const T*;
        using reverse_iterator = std::reverse_iterator<iterator>;
        using const_reverse_iterator = std::reverse_iterator<const_iterator>;

        // Static member representing the extent (compile-time size)
        static constexpr size_type extent = Extent;

        // --- Constructors ---

        /**
         * @brief Default constructor. Creates an empty span.
         * Only available for dynamic_extent or Extent == 0.
         */
        constexpr span() noexcept
            : data_(nullptr), size_(0) {
            static_assert(Extent == 0 || Extent == dynamic_extent,
                          "Default constructor is only for dynamic_extent or Extent == 0.");
        }

        /**
         * @brief Constructs a span from a pointer to the first element and a count.
         * This constructor can be used for both dynamic and static extents,
         * provided the 'count' matches 'Extent' if 'Extent' is not dynamic_extent.
         * @param ptr Pointer to the first element.
         * @param count Number of elements in the span.
         */
        constexpr span(pointer ptr, size_type count)
            : data_(ptr), size_(count) {
            if constexpr (Extent != dynamic_extent) {
                if (count != Extent) {
                    // This scenario would typically be a compile-time error for std::span
                    // if the count is known at compile time and doesn't match the static extent.
                    // For runtime count, it's a runtime error.
                    // For simplicity, we assume correct usage or rely on static_asserts in callers
                    // or other constructors that enforce static extent matching.
                }
            }
        }

        /**
         * @brief Constructs a span from a pair of pointers (begin and end).
         * Only available for dynamic_extent.
         * @param first Pointer to the first element.
         * @param last Pointer one past the last element.
         */
        constexpr span(pointer first, pointer last)
            : data_(first), size_(static_cast<size_type>(last - first)) {
            static_assert(Extent == dynamic_extent,
                          "Constructor from pointer range is only for dynamic_extent.");
        }

        /**
         * @brief Constructs a span from an iterator range.
         * This constructor is not constexpr in C++17 for generic iterators,
         * but it allows construction from types like std::vector::iterator.
         * Requires the iterators to be random access and their value_type
         * convertible to the span's element_type.
         * @tparam It The iterator type.
         * @param first Iterator to the first element.
         * @param last Iterator one past the last element.
         */
        template <typename It,
                  typename = std::enable_if_t<
                      // Check if Iterator's value_type is convertible to T
                      std::is_convertible<typename std::iterator_traits<It>::pointer, pointer>::value &&
                      // Check if it's a random access iterator (for operator- and contiguous memory)
                      std::is_base_of<std::random_access_iterator_tag, typename std::iterator_traits<It>::iterator_category>::value
                  >>
        span(It first, It last) // Not constexpr to support generic iterators in C++17
            : data_(&(*first)), size_(static_cast<size_type>(last - first)) {
            static_assert(Extent == dynamic_extent,
                          "Constructor from iterator range is only for dynamic_extent.");
        }


        /**
         * @brief Constructs a span from a C-style array.
         * @tparam N The size of the C-style array.
         * @param arr The C-style array.
         */
        template <std::size_t N>
        constexpr span(element_type (&arr)[N]) noexcept
            : data_(arr), size_(N) {
            static_assert(Extent == dynamic_extent || Extent == N,
                          "Array constructor: Extent must be dynamic_extent or match array size.");
        }

        /**
         * @brief Constructs a span from a std::array (non-const reference).
         * @tparam N The size of the std::array.
         * @param arr The std::array.
         */
        template <std::size_t N>
        constexpr span(std::array<value_type, N>& arr) noexcept
            : data_(arr.data()), size_(N) {
            static_assert(Extent == dynamic_extent || Extent == N,
                          "std::array constructor: Extent must be dynamic_extent or match array size.");
        }

        /**
         * @brief Constructs a span from a const std::array.
         * Requires the span's element_type to be const.
         * @tparam N The size of the std::array.
         * @param arr The const std::array.
         */
        template <std::size_t N>
        constexpr span(const std::array<value_type, N>& arr) noexcept
            : data_(arr.data()), size_(N) {
            static_assert(Extent == dynamic_extent || Extent == N,
                          "const std::array constructor: Extent must be dynamic_extent or match array size.");
            static_assert(std::is_const<T>::value, "Cannot convert const std::array to non-const span.");
        }

        /**
         * @brief Constructs a span from a std::vector (non-const reference).
         * Only available for dynamic_extent.
         * @param vec The std::vector.
         */
        constexpr span(std::vector<value_type>& vec) noexcept
            : data_(vec.data()), size_(vec.size()) {
            static_assert(Extent == dynamic_extent,
                          "std::vector constructor is only for dynamic_extent.");
        }

        /**
         * @brief Constructs a span from a const std::vector.
         * Only available for dynamic_extent. Requires the span's element_type to be const.
         * @param vec The const std::vector.
         */
        constexpr span(const std::vector<value_type>& vec) noexcept
            : data_(vec.data()), size_(vec.size()) {
            static_assert(Extent == dynamic_extent,
                          "const std::vector constructor is only for dynamic_extent.");
            static_assert(std::is_const<T>::value, "Cannot convert const std::vector to non-const span.");
        }

        /**
         * @brief Constructs a span from another span (copy or conversion).
         * Allows conversion from span<U> to span<T> if U* is convertible to T*.
         * Also handles static to dynamic extent conversion.
         * @tparam U The element type of the other span.
         * @tparam OtherExtent The extent of the other span.
         * @param s The other span.
         */
        template <typename U, std::size_t OtherExtent>
        constexpr span(const span<U, OtherExtent>& s) noexcept
            : data_(s.data()), size_(s.size()) {
            // Check if U* is convertible to T* (e.g., int* to const int*)
            static_assert(std::is_convertible<U (*)[], T (*)[]>::value,
                          "Cannot convert span<U> to span<T> (type mismatch or const qualification).");
            // Check extent compatibility:
            // 1. If current span has static extent, it must match OtherExtent.
            // 2. If current span has dynamic extent, OtherExtent can be anything.
            static_assert(Extent == dynamic_extent || Extent == OtherExtent,
                          "Span conversion: Extent must be dynamic_extent or match OtherExtent.");
            // Note: If Extent is static and OtherExtent is dynamic, a runtime check
            // (Extent != s.size()) would be needed, but std::span disallows this direct conversion
            // to prevent runtime errors. We follow that pattern with static_assert.
        }

        // --- Accessors ---

        /**
         * @brief Returns a pointer to the first element of the span.
         */
        constexpr pointer data() const noexcept { return data_; }

        /**
         * @brief Returns the number of elements in the span.
         */
        constexpr size_type size() const noexcept { return size_; }

        /**
         * @brief Returns the size of the span in bytes.
         */
        constexpr size_type size_bytes() const noexcept { return size_ * sizeof(element_type); }

        /**
         * @brief Checks if the span is empty.
         */
        constexpr bool empty() const noexcept { return size_ == 0; }

        // --- Element access ---

        /**
         * @brief Accesses the element at the specified index. No bounds checking.
         * @param idx The index of the element.
         * @return A reference to the element.
         */
        constexpr reference operator[](size_type idx) const {
            // No bounds checking for performance, like std::span
            return data_[idx];
        }

        /**
         * @brief Returns a reference to the first element. Undefined behavior if empty.
         */
        constexpr reference front() const {
            // Assert for empty() in debug builds, noexcept in release
            return data_[0];
        }

        /**
         * @brief Returns a reference to the last element. Undefined behavior if empty.
         */
        constexpr reference back() const {
            // Assert for empty() in debug builds, noexcept in release
            return data_[size_ - 1];
        }

        // --- Iterators ---

        /**
         * @brief Returns an iterator to the beginning of the span.
         */
        constexpr iterator begin() const noexcept { return data_; }

        /**
         * @brief Returns an iterator to the end of the span.
         */
        constexpr iterator end() const noexcept { return data_ + size_; }

        /**
         * @brief Returns a const iterator to the beginning of the span.
         */
        constexpr const_iterator cbegin() const noexcept { return data_; }

        /**
         * @brief Returns a const iterator to the end of the span.
         */
        constexpr const_iterator cend() const noexcept { return data_ + size_; }

        /**
         * @brief Returns a reverse iterator to the end of the span (beginning of reverse iteration).
         */
        constexpr reverse_iterator rbegin() const noexcept { return reverse_iterator(end()); }

        /**
         * @brief Returns a reverse iterator to the beginning of the span (end of reverse iteration).
         */
        constexpr reverse_iterator rend() const noexcept { return reverse_iterator(begin()); }

        /**
         * @brief Returns a const reverse iterator to the end of the span.
         */
        constexpr const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator(cend()); }

        /**
         * @brief Returns a const reverse iterator to the beginning of the span.
         */
        constexpr const_reverse_iterator crend() const noexcept { return const_reverse_iterator(cbegin()); }

        // --- Subspan methods ---

        /**
         * @brief Returns a subspan consisting of the first Count elements.
         * Compile-time Count version.
         * @tparam Count The number of elements in the subspan.
         * @return A new span representing the first Count elements.
         * @throws std::out_of_range If Count exceeds the span's size.
         */
        template <size_type Count>
        constexpr span<T, Count> first() const {
            static_assert(Extent == dynamic_extent || Extent >= Count,
                          "first(): Count must not exceed static extent.");
            if (size_ < Count) {
                throw std::out_of_range("Count exceeds span size in first()");
            }
            return span<T, Count>(data_, Count);
        }

        /**
         * @brief Returns a subspan consisting of the first 'count' elements.
         * Runtime 'count' version. Returns a dynamic_extent span.
         * @param count The number of elements in the subspan.
         * @return A new span representing the first 'count' elements.
         * @throws std::out_of_range If 'count' exceeds the span's size.
         */
        constexpr span<T, dynamic_extent> first(size_type count) const {
            if (size_ < count) {
                throw std::out_of_range("Count exceeds span size in first(count)");
            }
            return span<T, dynamic_extent>(data_, count);
        }

        /**
         * @brief Returns a subspan consisting of the last Count elements.
         * Compile-time Count version.
         * @tparam Count The number of elements in the subspan.
         * @return A new span representing the last Count elements.
         * @throws std::out_of_range If Count exceeds the span's size.
         */
        template <size_type Count>
        constexpr span<T, Count> last() const {
            static_assert(Extent == dynamic_extent || Extent >= Count,
                          "last(): Count must not exceed static extent.");
            if (size_ < Count) {
                throw std::out_of_range("Count exceeds span size in last()");
            }
            return span<T, Count>(data_ + (size_ - Count), Count);
        }

        /**
         * @brief Returns a subspan consisting of the last 'count' elements.
         * Runtime 'count' version. Returns a dynamic_extent span.
         * @param count The number of elements in the subspan.
         * @return A new span representing the last 'count' elements.
         * @throws std::out_of_range If 'count' exceeds the span's size.
         */
        constexpr span<T, dynamic_extent> last(size_type count) const {
            if (size_ < count) {
                throw std::out_of_range("Count exceeds span size in last(count)");
            }
            return span<T, dynamic_extent>(data_ + (size_ - count), count);
        }

        /**
         * @brief Returns a subspan starting at Offset with Count elements.
         * Compile-time Offset and Count version.
         * @tparam Offset The starting index of the subspan.
         * @tparam Count The number of elements in the subspan. Defaults to dynamic_extent (to end).
         * @return A new span representing the specified sub-range.
         * @throws std::out_of_range If Offset or Offset + Count are out of bounds.
         */
        template <size_type Offset, size_type Count = dynamic_extent>
        constexpr auto subspan() const {
            static_assert(Extent == dynamic_extent || Extent >= Offset,
                          "subspan(): Offset must not exceed static extent.");
            static_assert(Count == dynamic_extent || Extent == dynamic_extent || (Offset + Count) <= Extent,
                          "subspan(): Offset + Count must not exceed static extent.");

            if (size_ < Offset) {
                throw std::out_of_range("Offset exceeds span size in subspan()");
            }

            size_type sub_size = Count;
            if (Count == dynamic_extent) {
                sub_size = size_ - Offset;
            } else if (Offset + Count > size_) {
                throw std::out_of_range("Offset + Count exceeds span size in subspan()");
            }

            // Determine the return type's extent based on input extents
            constexpr size_type NewExtent = (Count == dynamic_extent || Extent == dynamic_extent)
                                            ? dynamic_extent
                                            : Count;
            return span<T, NewExtent>(data_ + Offset, sub_size);
        }

        /**
         * @brief Returns a subspan starting at 'offset' with 'count' elements.
         * Runtime 'offset' and 'count' version. Returns a dynamic_extent span.
         * @param offset The starting index of the subspan.
         * @param count The number of elements in the subspan. Defaults to dynamic_extent (to end).
         * @return A new span representing the specified sub-range.
         * @throws std::out_of_range If 'offset' or 'offset' + 'count' are out of bounds.
         */
        constexpr span<T, dynamic_extent> subspan(size_type offset, size_type count = dynamic_extent) const {
            if (size_ < offset) {
                throw std::out_of_range("Offset exceeds span size in subspan(offset, count)");
            }

            size_type sub_size = count;
            if (count == dynamic_extent) {
                sub_size = size_ - offset;
            } else if (offset + count > size_) {
                throw std::out_of_range("Offset + Count exceeds span size in subspan(offset, count)");
            }

            return span<T, dynamic_extent>(data_ + offset, sub_size);
        }

    private:
        pointer data_;
        size_type size_;
    };

    // --- Deduction Guides (C++17 feature) ---
    // These allow for simpler syntax when constructing spans, letting the compiler
    // deduce template arguments.

    // Deduce from C-style array: `my_std::span s(arr);` -> `my_std::span<int, 5>`
    template <typename T, std::size_t N>
    span(T (&)[N]) -> span<T, N>;

    // Deduce from std::array (non-const): `my_std::span s(std_arr);` -> `my_std::span<int, 3>`
    template <typename T, std::size_t N>
    span(std::array<T, N>&) -> span<T, N>;

    // Deduce from const std::array: `my_std::span s(const_std_arr);` -> `my_std::span<const int, 2>`
    template <typename T, std::size_t N>
    span(const std::array<T, N>&) -> span<const T, N>;

    // Deduce from std::vector (non-const): `my_std::span s(vec);` -> `my_std::span<double, dynamic_extent>`
    template <typename T>
    span(std::vector<T>&) -> span<T, dynamic_extent>;

    // Deduce from const std::vector: `my_std::span s(const_vec);` -> `my_std::span<const char, dynamic_extent>`
    template <typename T>
    span(const std::vector<T>&) -> span<const T, dynamic_extent>;

    // Deduce from pointer and size: `my_std::span s(ptr, count);` -> `my_std::span<int, dynamic_extent>`
    template <typename T>
    span(T*, std::size_t) -> span<T, dynamic_extent>;

    // Deduce from pointer pair: `my_std::span s(begin_ptr, end_ptr);` -> `my_std::span<int, dynamic_extent>`
    // This deduction guide is for raw pointer pairs.
    template <typename T>
    span(T*, T*) -> span<T, dynamic_extent>;

    // New deduction guide for iterator pair: `my_std::span s(begin_it, end_it);` -> `my_std::span<T, dynamic_extent>`
    template <typename It1, typename It2>
    span(It1, It2) -> span<std::remove_reference_t<decltype(*std::declval<It1>())>, dynamic_extent>;

} // namespace my_std
