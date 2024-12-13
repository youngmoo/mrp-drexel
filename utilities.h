#pragma once
#include <typeinfo>

// a little helper to make it safer to get the type of the object pointed to by
// a ptr. Also prevents generating clang's "-wWpotentially-evaluated-expression"
// warnings.
template <typename T> const std::type_info& get_ptr_typeid(const T* v) {
    if (v)
        return typeid(*v);
    else
        return typeid(void);
}
