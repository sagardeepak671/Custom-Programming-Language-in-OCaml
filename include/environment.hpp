#pragma once
#include "mydsl.hpp"

namespace mydsl {
// Scopes are stack-owned; a child never outlives its parent.
template<class T> struct Environment {
    Environment* parent = nullptr;
    std::map<std::string, T> symbols;
    explicit Environment(Environment* outer = nullptr) : parent(outer) {}
    T* find(const std::string& name) {
        auto it = symbols.find(name);
        if (it != symbols.end()) return &it->second;
        return parent ? parent->find(name) : nullptr;
    }
};
} // namespace mydsl
