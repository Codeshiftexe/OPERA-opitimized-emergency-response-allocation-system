#pragma once

#include <cstddef>
#include <stdexcept>
#include <utility>

namespace opera {

template <typename T>
class Stack {
public:
    Stack() = default;

    ~Stack() { clear(); }

    Stack(const Stack& other) {
        // Appending at the tail keeps the original order. Pushing each value
        // instead would reverse the copy.
        Node** tail = &top_;
        try {
            for (Node* n = other.top_; n != nullptr; n = n->next) {
                *tail = new Node{n->value, nullptr};
                tail = &(*tail)->next;
                ++size_;
            }
        } catch (...) {
            clear();
            throw;
        }
    }

    Stack(Stack&& other) noexcept : top_(other.top_), size_(other.size_) {
        other.top_ = nullptr;
        other.size_ = 0;
    }

    // Copy-and-swap: "other" is already a copy (or a moved-from temporary),
    // so swapping with it covers copy, move and self-assignment in one place.
    Stack& operator=(Stack other) noexcept {
        swap(other);
        return *this;
    }

    void swap(Stack& other) noexcept {
        std::swap(top_, other.top_);
        std::swap(size_, other.size_);
    }

    void push(const T& value) {
        top_ = new Node{value, top_};
        ++size_;
    }

    void push(T&& value) {
        top_ = new Node{std::move(value), top_};
        ++size_;
    }

    T pop() {
        if (top_ == nullptr) {
            throw std::out_of_range("Stack::pop - stack is empty");
        }
        T value = std::move(top_->value);
        Node* old = top_;
        top_ = top_->next;
        delete old;
        --size_;
        return value;
    }

    const T& top() const {
        if (top_ == nullptr) {
            throw std::out_of_range("Stack::top - stack is empty");
        }
        return top_->value;
    }

    T& top() {
        if (top_ == nullptr) {
            throw std::out_of_range("Stack::top - stack is empty");
        }
        return top_->value;
    }

    // Iterative on purpose: a recursive delete would overflow the call stack
    // on very long histories.
    void clear() {
        while (top_ != nullptr) {
            Node* next = top_->next;
            delete top_;
            top_ = next;
        }
        size_ = 0;
    }

    std::size_t size() const { return size_; }
    bool empty() const { return top_ == nullptr; }

private:
    struct Node {
        T value;
        Node* next;
    };

    Node* top_ = nullptr;
    std::size_t size_ = 0;
};

}  
