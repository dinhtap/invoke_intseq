#ifndef INVOKE_INTSEQ_H
#define INVOKE_INTSEQ_H

#include <utility>
#include <functional>
#include <type_traits>

namespace kt {
    /*  In every recursive call to go through all args we circular-shift all 
    agrs after processing one. Processed args are at the end.
    */ 

    /* struct is_int_seq
    Checks whether type is std::integer_sequence. */
    template<typename T>
    struct is_int_seq : std::false_type {};

    template<typename T, T... Ins>
    struct is_int_seq<std::integer_sequence<T, Ins...>> : std::true_type {};

    template<typename T>// For const, & etc. 
    concept int_seq = is_int_seq<typename std::decay_t<T>>::value;
    // End struct is_int_seq.


    /* struct get_return_type
    Checks what type does function F return.
    Recursively converts all std::integer_sequence to std::integral_constant
    while checking.
    Every struct takes 2 size_t: how many args converted, how many args overall.
    */
    template<size_t argcount, size_t totalarg, class F, class... Args>
    struct get_return_type {};

    // All args were converted.
    template<size_t count, class F, class... Args>
    struct get_return_type<count, count, F, Args...> {
        using type = typename std::invoke_result_t<F, Args...>;
    };

    // Not std::integer_sequence.
    template<size_t argcount, size_t totalarg, class F, class CurArg,
                                                                class... Args> 
    requires ((argcount < totalarg) && !int_seq<CurArg>)
    struct get_return_type<argcount, totalarg, F, CurArg, Args...> {
        using type = typename get_return_type<argcount + 1, totalarg,
                                                    F, Args..., CurArg>::type;
    };

    // std::integer_sequence (with const, &, etc.).
    template<size_t argcount, size_t totalarg, class F, class CurArg,
                                                                 class... Args>
    requires (int_seq<CurArg> && (argcount < totalarg))
    struct get_return_type<argcount, totalarg, F, CurArg, Args...> {
        using type = typename get_return_type<argcount, totalarg, F, 
                                typename std::decay_t<CurArg>, Args...>::type;
    };
    // Convert std::integer_sequence to std::integral_constant.
    template<size_t argcount, size_t totalarg, class F, class Int, Int... ints,
                                                                 class... Args>
    struct get_return_type<argcount, totalarg, F, 
                                std::integer_sequence<Int, ints...>, Args...> {
        using type = typename get_return_type<argcount + 1, totalarg, F,
                                 Args..., std::integral_constant<Int, 0>>::type;
    };
    // End struct get_return_type


    /* struct invoke_details
    Counts how many times to invoke function and detects presence of 
    std::integer_sequence in args. */
    // No arg
    template<typename... T>
    struct invoke_details {
        static constexpr size_t count() {
            return 1;
        }

        static constexpr bool seq_presence = false;
    };
    // Not std::integer_sequence.
    template<class CurArg, class... RemArgs>
    struct invoke_details<CurArg, RemArgs...> {
        static constexpr size_t count() {
            return invoke_details<RemArgs...>::count();
        }

        static constexpr bool seq_presence = 
                                       invoke_details<RemArgs...>::seq_presence;
    };
    // Got std::integer_sequence (with const, &, etc.).
    template<int_seq CurArg, class... RemArgs>
    struct invoke_details<CurArg, RemArgs...> {
        static constexpr size_t count() {
            return std::decay_t<CurArg>::size() *
                                            invoke_details<RemArgs...>::count();
        }

        static constexpr bool seq_presence = true;
    };
    // End struct invoke_details.


    /* struct first_invoke
    First invoke of all invokes, in order to construct an array of F's 
    return-type (in case it does not have default constructor) 
    */
    template<class... T>
    struct first_invoke {};

    /* Template arguments are remaining types to process */
    template<>
    struct first_invoke<> {
        template<class F, class... Args>
        static constexpr decltype(auto) inv(F &&f, Args &&... args) {
            return std::invoke(std::forward<F>(f), std::forward<Args>(args)...);
        }
    };

    /* Not std::integer_sequence. */
    template<class CurArg, class... RemArgs>
    struct first_invoke<CurArg, RemArgs...> {
        template<class F, class... OtherArgs>
        static constexpr decltype(auto) inv(F &&f, CurArg &&curArg,
                                                    OtherArgs&&... otherArgs) {
            return first_invoke<RemArgs...>::inv(std::forward<F>(f),
                                        std::forward<OtherArgs>(otherArgs)...,
                                        std::forward<CurArg>(curArg));
        }
    };

    /* Got std::integer_sequence (with const, &, etc.) */
    template<int_seq CurArg, class... RemArgs>
    struct first_invoke<CurArg, RemArgs...> {
        using decay_seq = typename std::decay_t<CurArg>;

        template<class F, class... OtherArgs>
        static constexpr decltype(auto) inv(F &&f, CurArg &&arg,
                                                    OtherArgs&&... otherArgs) {
            return first_invoke<decay_seq, RemArgs...>::inv(std::forward<F>(f),
                                        static_cast<decay_seq&&>(arg),
                                        std::forward<OtherArgs>(otherArgs)...);
        }
    };
    // Replacing std::integer_sequence with std::integral_constant.
    template<class Int, Int first, Int... ints, class... RemArgs>
    struct first_invoke<std::integer_sequence<Int, first, ints...>, RemArgs...> {
        template<class F, class... OtherArgs>
        static constexpr decltype(auto) inv(F &&f,
        std::integer_sequence<Int, first, ints...> &&, OtherArgs &&... otherArgs){
            return first_invoke<RemArgs...>::inv(std::forward<F>(f),
                                        std::forward<OtherArgs>(otherArgs)...,
                                        std::integral_constant<Int, first>());
        }
    };
    // End struct first_invoke.


    /* struct remaining_invokes
    Does the remaining invokes after the first one. Works similarly to first_invoke. */
    template<class... T>
    struct remaining_invokes {};
    
    /* inv saves invokes' results to the array given as argument.
    inv_void is for f returning void. */
    template<>
    struct remaining_invokes<> {
        template<class F, class T, size_t n, class... Args>
        static constexpr void inv(size_t *curindex, std::array<T, n> *array,
                                                       F &&f, Args &&... args) {
            if (*curindex > 0) {// Ominiemy pierwsze wywolanie.
                if constexpr (std::is_reference_v<
                                        decltype(std::invoke(std::forward<F>(f),
                                        std::forward<Args>(args)...))>) {
                    (*array)[*curindex] = std::reference_wrapper(std::invoke(
                              std::forward<F>(f), std::forward<Args>(args)...));
                } 
                else {
                    (*array)[*curindex] = std::invoke(std::forward<F>(f),
                                                   std::forward<Args>(args)...);
                }
            }
            (*curindex)++;
        }

        template<class F, class... Args>
        static constexpr void inv_void(F &&f, Args &&... args) {
            std::invoke(std::forward<F>(f), std::forward<Args>(args)...);
        }
    };

    template<class CurArg, class... RemArgs>
    struct remaining_invokes<CurArg, RemArgs...> {
        template<class F, class T, size_t n, class... OtherArgs>
        static constexpr void inv(size_t *curindex, std::array<T, n> *array,
                            F &&f, CurArg &&curArg, OtherArgs &&... otherArgs) {
            remaining_invokes<RemArgs...>::inv(std::forward<size_t *>(curindex),
                                        std::forward<std::array<T, n> *>(array),
                                        std::forward<F>(f),
                                        std::forward<OtherArgs>(otherArgs)...,
                                        std::forward<CurArg>(curArg));
        }

        template<class F, class... OtherArgs>
        static constexpr void inv_void(F &&f, CurArg &&curArg,
                                                    OtherArgs &&... otherArgs) {
            remaining_invokes<RemArgs...>::inv_void(std::forward<F>(f),
                                          std::forward<OtherArgs>(otherArgs)...,
                                          std::forward<CurArg>(curArg));
        }
    };

    template<int_seq CurArg, class... RemArgs>
    struct remaining_invokes<CurArg, RemArgs...> {
        using decay_seq = typename std::decay_t<CurArg>;

        template<class F, class T, size_t n, class... OtherArgs>
        static constexpr void inv(size_t *curindex, std::array<T, n> *array,
                               F &&f, CurArg &&arg, OtherArgs &&... otherArgs) {
            remaining_invokes<decay_seq, RemArgs...>::inv(
                                        std::forward<size_t *>(curindex),
                                        std::forward<std::array<T, n> *>(array),
                                        std::forward<F>(f),
                                        static_cast<decay_seq&&>(arg),
                                        std::forward<OtherArgs>(otherArgs)...);
        }

        template<class F, class... OtherArgs>
        static constexpr void inv_void(F &&f, CurArg &&arg,
                                                    OtherArgs &&... otherArgs) {
            remaining_invokes<decay_seq, RemArgs...>::inv_void(
                                        std::forward<F>(f),
                                        static_cast<decay_seq&&>(arg),
                                        std::forward<OtherArgs>(otherArgs)...);
        }
    };

    template<class Int, Int... ints, class... RemArgs>
    struct remaining_invokes<std::integer_sequence<Int, ints...>, RemArgs...> {
        template<class F, class T, size_t n, class... OtherArgs>
        static constexpr void inv(size_t *curindex, std::array<T, n> *array,
                                F &&f, std::integer_sequence<Int, ints...> &&,
                                                    OtherArgs &&... otherArgs) {
           (remaining_invokes<RemArgs...>::inv(std::forward<size_t *>(curindex),
                                        std::forward<std::array<T, n> *>(array),
                                        std::forward<F>(f), 
                                        std::forward<OtherArgs>(otherArgs)...,
                                        std::integral_constant<Int, ints>()),
                                                                           ...);
        }

        template<class F, class... OtherArgs>
        static constexpr void inv_void(F &&f, 
                                       std::integer_sequence<Int, ints...> &&, 
                                       OtherArgs &&... otherArgs) {
            (remaining_invokes<RemArgs...>::inv_void(std::forward<F>(f), 
                                          std::forward<OtherArgs>(otherArgs)...,
                                          std::integral_constant<Int, ints>()), 
                                                                           ...);
        }
    };
    // End struct remaining_invokes.

    // Create and fill a new array with "value"
    template<typename T, std::size_t ... Is>
    constexpr std::array<T, sizeof...(Is)> fill_array(T value,
                                                   std::index_sequence<Is...>) {
        return {{(static_cast<void>(Is), value)...}};
    }

}// namespace kt


template<class F, class... Args>
constexpr decltype(auto) invoke_intseq(F &&f, Args &&... args) {
    using return_type = typename kt::get_return_type<0, sizeof...(Args), F,
                                                                 Args...>::type;

    if constexpr (std::is_same_v<return_type, void>) {
        return kt::remaining_invokes<Args...>::inv_void(std::forward<F>(f),
                                                   std::forward<Args>(args)...);
    } 
    else {
        constexpr size_t invokecount = kt::invoke_details<Args...>::count();
        constexpr bool seq_presence = kt::invoke_details<Args...>::seq_presence;

        if constexpr (invokecount == 0) {
            return std::array<return_type, 0>();
        } 
        // invokecount > 0
        else if constexpr (std::is_reference_v<return_type>) {
            auto &first_ret = kt::first_invoke<Args...>::inv(std::forward<F>(f),
                                                   std::forward<Args>(args)...);
            if constexpr (!seq_presence) {
                return first_ret;
            } 
            else {
                size_t curindx = 0;
                auto results = kt::fill_array(std::reference_wrapper(first_ret),
                                       std::make_index_sequence<invokecount>());
                kt::remaining_invokes<Args...>::inv(&curindx, &results,
                               std::forward<F>(f), std::forward<Args>(args)...);
                return results;
            }
        } 
        else {// return_type is not reference
            auto first_ret = kt::first_invoke<Args...>::inv(std::forward<F>(f),
                                                   std::forward<Args>(args)...);
            if constexpr (!seq_presence) {
                return first_ret;
            } 
            else {
                size_t curindx = 0;
                auto results = kt::fill_array(first_ret,
                                       std::make_index_sequence<invokecount>());
                kt::remaining_invokes<Args...>::inv(&curindx, &results,
                               std::forward<F>(f), std::forward<Args>(args)...);
                return results;
            }
        }
    }
}

#endif //INVOKE_INTSEQ_H