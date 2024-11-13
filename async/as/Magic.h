// SPDX-FileCopyrightText: 2016-2024 Technical University of Munich
//
// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file
 *  This file is part of ASYNC
 *
 * @author Sebastian Rettenberger <sebastian.rettenberger@tum.de>
 */

#ifndef ASYNC_AS_MAGIC_H
#define ASYNC_AS_MAGIC_H

namespace async::as {

/**
 * @class      : ASYNC_HAS_MEM_FUNC
 * @brief      : This macro will be used to check if a class has a particular
 * member function implemented in the public section or not.
 * @param func : Name of Member Function
 * @param name : Name of struct which is going to be run the test for
 * the given particular member function name specified in func
 * @param return_type: Return type of the member function
 * @param ellipsis(...) : Since this is macro should provide test case for every
 * possible member function we use variadic macros to cover all possibilities
 *
 * source:
 * http://stackoverflow.com/questions/257288/is-it-possible-to-write-a-c-template-to-check-for-a-functions-existence
 * Modified to allow for an additional template function.
 */
#define ASYNC_HAS_MEM_FUNC_T1(func, name, T1, return_type, ...)                                    \
  template <typename T, typename T1>                                                               \
  struct name {                                                                                    \
    using Sign = auto (T::*)(__VA_ARGS__) -> return_type;                                          \
    using Yes = double;                                                                            \
    using No = float;                                                                              \
    template <typename U, U>                                                                       \
    struct type_check;                                                                             \
    template <typename Dummy>                                                                      \
    static auto chk(type_check<Sign, &Dummy::func>*) -> Yes&;                                      \
    template <typename>                                                                            \
    static auto chk(...) -> No&;                                                                   \
    static bool const Value = sizeof(chk<T>(nullptr)) == sizeof(Yes);                              \
  };

} // namespace async::as

#endif // ASYNC_AS_MAGIC_H
