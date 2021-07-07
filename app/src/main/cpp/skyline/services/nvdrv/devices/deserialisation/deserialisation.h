// SPDX-License-Identifier: MIT
// Copyright © 2021 Skyline Team and Contributors (https://github.com/skyline-emu/)

#pragma once

#include <type_traits>
#include <common.h>
#include "types.h"

namespace skyline::service::nvdrv::deserialisation {
    template<typename Desc, typename ArgType> requires (Desc::Out && Desc::In && IsInOut<ArgType>::value)
    constexpr ArgType DecodeArgument(span<u8, Desc::Size> buffer, size_t &offset) {
        offset += sizeof(RemoveInOut<ArgType>);
        return buffer.subspan(offset).template as<RemoveInOut<ArgType>>();
    }

    template<typename Desc, typename ArgType> requires (Desc::In && IsIn<ArgType>::value)
    constexpr ArgType DecodeArgument(span<u8, Desc::Size> buffer, size_t &offset) {
        offset += sizeof(ArgType);
        return buffer.subspan(offset).template as<ArgType>();
    }

    template<typename Desc, typename ArgType> requires (Desc::Out && IsOut<ArgType>::value)
    constexpr ArgType DecodeArgument(span<u8, Desc::Size> buffer, size_t &offset) {
        offset += sizeof(RemoveOut<ArgType>);
        return Out(buffer.subspan(offset).template as<RemoveOut<ArgType>>());
    }

    template <typename...Ts>
    constexpr std::tuple<Ts...> make_ref_tuple(Ts&&...ts) {
        return std::tuple<Ts...>{std::forward<Ts>(ts)...};
    }

    template<typename Desc, typename ArgType, typename... ArgTypes>
    constexpr auto DecodeArgumentsImpl(span<u8, Desc::Size> buffer, size_t &offset) {
        if constexpr (IsPad<ArgType>::value) {
            offset += ArgType::Bytes;
            return DecodeArgumentsImpl<Desc, ArgTypes...>(buffer, offset);
        } else {
            if constexpr(sizeof...(ArgTypes) == 0) {
                return make_ref_tuple(DecodeArgument<Desc, ArgType>(buffer, offset));
            } else {
                return std::tuple_cat(make_ref_tuple(DecodeArgument<Desc, ArgType>(buffer, offset)),
                                      DecodeArgumentsImpl<Desc, ArgTypes...>(buffer, offset));
            }
        }
    }

    template<typename Desc, typename... ArgTypes>
    constexpr auto DecodeArguments(span<u8, Desc::Size> buffer) {
        size_t offset;
        return DecodeArgumentsImpl<Desc, ArgTypes...>(buffer, offset);
    }
}