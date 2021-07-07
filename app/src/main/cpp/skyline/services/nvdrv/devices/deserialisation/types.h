// SPDX-License-Identifier: MIT
// Copyright © 2021 Skyline Team and Contributors (https://github.com/skyline-emu/)

#pragma once

#include <concepts>
#include <common.h>
#include <services/nvdrv/types.h>

namespace skyline::service::nvdrv::deserialisation {
    // IOCTL parameters can't be pointers and must be trivially copyable
    template<typename T>
    concept BufferData = std::is_trivially_copyable_v<T> && !std::is_pointer_v<T>;

    // Input IOCTL template types
    template<typename T> requires BufferData<T>
    using In = const T;

    template<typename>
    struct IsIn : std::false_type {};

    template<typename T>
    struct IsIn<In<T>> : std::true_type {};


    // Input/Output IOCTL template types
    template<typename T> requires BufferData<T>
    using InOut = T &;

    template<typename>
    struct IsInOut : std::false_type {};

    template<typename T>
    struct IsInOut<InOut<T>> : std::true_type {};

    template<typename T> requires IsInOut<T>::value
    using RemoveInOut = typename std::remove_reference<T>::type;


    // Output IOCTL template types
    template<typename T> requires BufferData<T>
    class Out {
      private:
        T &ref;

      public:
        Out(T &ref) : ref(ref) {}

        using ValueType = T;

        Out& operator=(T val) {
            ref = std::move(val);
            return *this;
        }
    };

    template<typename>
    struct IsOut : std::false_type {};

    template<typename T>
    struct IsOut<Out<T>> : std::true_type {};

    template<typename T> requires IsOut<T>::value
    using RemoveOut = typename T::ValueType;


    // Span IOCTL template types
    template<typename T> requires BufferData<T>
    using InSpan = span<const T>;

    template<typename T> requires BufferData<T>
    using InOutSpan = span<T>;

    template<typename T> requires BufferData<T>
    using OutSpan = span<T>;


    // Padding template type
    template<typename T, size_t Count = 1> requires BufferData<T>
    struct Pad {
        static constexpr auto Bytes{Count * sizeof(T)};
    };

    template<typename>
    struct IsPad : std::false_type {};

    template<typename T, size_t TCount>
    struct IsPad<Pad<T, TCount>> : std::true_type {};

    /**
     * @brief Describes an IOCTL as a type for use in deserialisation
     */
    template <bool TOut, bool TIn, u16 TSize, i8 TMagic, u8 TFunction>
    struct MetaIoctlDescriptor {
        static constexpr auto Out{TOut};
        static constexpr auto In{TIn};
        static constexpr auto Size{TSize};
        static constexpr auto Magic{TMagic};
        static constexpr auto Function{TFunction};

        constexpr operator IoctlDescriptor() const {
            return {Out, In, Size, Magic, Function};
        }

        constexpr static u32 Raw() {
            u32 raw{Function};

            i8 offset{8};
            raw |= Magic << offset; offset += 8;
            raw |= Size << offset; offset += 14;
            raw |= In << offset; offset++;
            raw |= Out << offset; offset++;
            return raw;
        }
    };
}
