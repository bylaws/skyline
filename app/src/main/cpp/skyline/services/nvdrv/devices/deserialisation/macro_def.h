// SPDX-License-Identifier: MIT
// Copyright © 2021 Skyline Team and Contributors (https://github.com/skyline-emu/)

#include "deserialisation.h"

#define IOCTL_HANDLER_FUNC(name, cases) \
    PosixResult name::Ioctl(IoctlDescriptor cmd, span<u8> buffer) { \
        using className = name;                                     \
        switch (cmd.raw) {                                          \
            cases;                                                  \
            default:                                                \
                return PosixResult::InappropriateIoctlForDevice;    \
        }                                                           \
    }


#define IOCTL_CASE_ARGS_I(in, out, size, magic, function, name, ...)                        \
    case MetaIoctlDescriptor<in, out, size, magic, function>::Raw(): {                      \
        using IoctlType = MetaIoctlDescriptor<in, out, size, magic, function>;              \
        auto args = DecodeArguments<IoctlType, __VA_ARGS__>(buffer.subspan<0, size>());     \
        return std::apply(&className::name, std::tuple_cat(std::make_tuple(this), args));   \
    }

#define IOCTL_CASE_NOARGS_I(in, out, size, magic, function, name)       \
    case MetaIoctlDescriptor<in, out, size, magic, function>::Raw():    \
        return className::name();

#define IOCTL_CASE_RESULT_I(in, out, size, magic, function, result)     \
    case MetaIoctlDescriptor<in, out, size, magic, function>::Raw():    \
        return result;

#define IOCTL_CASE_ARGS(...) IOCTL_CASE_ARGS_I(__VA_ARGS__)
#define IOCTL_CASE_NOARGS(...) IOCTL_CASE_NOARGS_I(__VA_ARGS__)
#define IOCTL_CASE_RESULT(...) IOCTL_CASE_RESULT_I(__VA_ARGS__)

#define IN true, false
#define OUT false, true
#define INOUT true, true
#define NONE false, false
#define SIZE(size) size
#define FUNC(func) func
#define MAGIC(magic) magic
#define ARGS(...) __VA_ARGS__
